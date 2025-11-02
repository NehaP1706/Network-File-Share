#include "common.h"
#include "logger.h"
#include "file_ops.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <sys/time.h>

#define SS_STORAGE_DIR "./ss_storage"
#define HEARTBEAT_INTERVAL 5
#define SENTENCE_CAPACITY 10
#define SOCKET_TIMEOUT 10  

static pthread_mutex_t nm_comm_mutex = PTHREAD_MUTEX_INITIALIZER; // Newly added to deal with heartbeats - N

typedef struct {
    int id;
    char ip[INET_ADDRSTRLEN];
    int nm_port;
    int client_port;
    int nm_sock;                 // NEW - command socket to NM - N
    int nm_hb_sock;              // NEW - for heartbeats - N
    int client_sock;
    char storage_path[MAX_PATH];
    
    pthread_mutex_t locks_mutex;
    struct {
        char filename[MAX_FILENAME];
        SentenceLock *locks;
        int lock_count;
    } file_locks[MAX_FILES];
    int file_lock_count;
    
    volatile int running;
} StorageServer;

StorageServer ss;

void* handle_nm_communication(void* arg);
void* handle_client_request(void* arg);
void* client_listener(void* arg);
void* heartbeat_thread(void* arg);
void scan_and_register_files();
int create_file_ss(const char *filename);
int delete_file_ss(const char *filename);
int read_file_ss(const char *filename, char *buffer);
int write_file_ss(const char *filename, int sent_idx, int word_idx, const char *content);
int stream_file_ss(int client_sock, const char *filename);
int get_file_info_ss(const char *filename, FileMetadata *meta);
SentenceLock* get_sentence_lock(const char *filename, int sentence_idx);
void init_file_locks(const char *filename, int sentence_count);
int lock_sentence_ss(const char *filename, int sent_idx, const char *username);
int unlock_sentence_ss(const char *filename, int sent_idx, const char *username);

int get_system_ip(char *ip_buffer, size_t buffer_size) {
    struct ifaddrs *ifaddr, *ifa;
    int found = 0;
    
    if (getifaddrs(&ifaddr) == -1) {
        perror("getifaddrs");
        return -1;
    }
    
    // Iterate through the network interfaces
    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL)
            continue;
        
        // Check for IPv4 address
        if (ifa->ifa_addr->sa_family == AF_INET) {
            struct sockaddr_in *addr = (struct sockaddr_in *)ifa->ifa_addr;
            char *ip = inet_ntoa(addr->sin_addr);
            
            // Skip loopback interface (127.0.0.1)
            if (strcmp(ip, "127.0.0.1") != 0) {
                strncpy(ip_buffer, ip, buffer_size - 1);
                ip_buffer[buffer_size - 1] = '\0';
                found = 1;
                break;
            }
        }
    }
    
    freeifaddrs(ifaddr);
    
    if (!found) {
        // Fallback to loopback if no other interface found
        strncpy(ip_buffer, "127.0.0.1", buffer_size - 1);
        ip_buffer[buffer_size - 1] = '\0';
        return -1;
    }
    
    return 0;
}

void init_storage_server(const char *nm_ip, int nm_port, int client_port, int ss_id) {
    
    if (get_system_ip(ss.ip, sizeof(ss.ip)) != 0) {
        fprintf(stderr, "[SS] Warning: Could not determine system IP, using loopback\n");
    }

    ss.nm_port = nm_port;
    ss.client_port = client_port;
    printf("[SS] System client port: %d\n", ss.client_port);
    ss.id = ss_id;
    ss.running = 1;
    
    snprintf(ss.storage_path, sizeof(ss.storage_path), "%s_%d", SS_STORAGE_DIR, ss.id);
    
    struct stat st = {0};
    if (stat(ss.storage_path, &st) == -1) {
        mkdir(ss.storage_path, 0777);
        printf("[SS %d] Created new storage directory: %s\n", ss.id, ss.storage_path);
    } else {
        printf("[SS %d] Using existing storage directory: %s\n", ss.id, ss.storage_path);
    }
    
    pthread_mutex_init(&nm_comm_mutex, NULL);
    pthread_mutex_init(&ss.locks_mutex, NULL);
    ss.file_lock_count = 0;

    char instance_name[64];
    snprintf(instance_name, sizeof(instance_name), "SS_%d", ss.id);
    set_instance_name(instance_name);
    
    char log_file[128];
    snprintf(log_file, sizeof(log_file), "ss_%d.log", ss.id);
    init_logger(log_file);
    
    printf("[SS %d] Storage Server initialized\n", ss.id);
    printf("[SS %d] Connecting to Name Server at %s:%d\n", ss.id, nm_ip, nm_port);
    printf("[SS %d] Storage path: %s\n", ss.id, ss.storage_path);
    printf("[SS %d] Client port: %d\n", ss.id, ss.client_port);
}

// Added doe to setup socket options for NM communication, might be unnecessary - N
void setup_nm_socket_options() {
    // Enable TCP keepalive to detect dead connections
    int keepalive = 1;
    setsockopt(ss.nm_sock, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive));
    
    // Set keepalive parameters (Linux-specific)
    #ifdef __linux__
    int keepidle = 10;   // Start probes after 10 seconds of idle
    int keepintvl = 5;   // Send probes every 5 seconds
    int keepcnt = 3;     // Close after 3 failed probes
    setsockopt(ss.nm_sock, IPPROTO_TCP, TCP_KEEPIDLE, &keepidle, sizeof(keepidle));
    setsockopt(ss.nm_sock, IPPROTO_TCP, TCP_KEEPINTVL, &keepintvl, sizeof(keepintvl));
    setsockopt(ss.nm_sock, IPPROTO_TCP, TCP_KEEPCNT, &keepcnt, sizeof(keepcnt));
    #endif
    
    log_formatted(LOG_INFO, "Socket keepalive configured");
}

void connect_to_nm(const char *nm_ip, int nm_port) {
    // Command socket
    ss.nm_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (ss.nm_sock < 0) {
        perror("Socket creation failed");
        exit(1);
    }
    
    struct sockaddr_in nm_addr;
    nm_addr.sin_family = AF_INET;
    nm_addr.sin_port = htons(nm_port);
    inet_pton(AF_INET, nm_ip, &nm_addr.sin_addr);
    
    if (connect(ss.nm_sock, (struct sockaddr*)&nm_addr, sizeof(nm_addr)) < 0) {
        perror("Connection to NM failed");
        exit(1);
    }
    setup_nm_socket_options();
    
    // Heartbeat socket configs - N
    ss.nm_hb_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (ss.nm_hb_sock < 0) {
        perror("Heartbeat socket creation failed");
        exit(1);
    }
    
    struct sockaddr_in hb_addr;
    hb_addr.sin_family = AF_INET;
    hb_addr.sin_port = htons(NM_SS_HB_PORT);
    inet_pton(AF_INET, nm_ip, &hb_addr.sin_addr);
    
    // Start collecting heartbeats from this - N
    if (connect(ss.nm_hb_sock, (struct sockaddr*)&hb_addr, sizeof(hb_addr)) < 0) {
        perror("Connection to NM heartbeat port failed");
        close(ss.nm_sock);
        exit(1);
    }
    
    printf("[SS %d] Connected to Name Server at %s:%d (cmd) and %s:%d (hb)\n", 
           ss.id, nm_ip, nm_port, nm_ip, NM_SS_HB_PORT);
    log_formatted(LOG_INFO, "Connected to NM at %s:%d (cmd) and %s:%d (hb)", 
                  nm_ip, nm_port, nm_ip, NM_SS_HB_PORT);
}

void scan_and_register_files() {
    DIR *dir = opendir(ss.storage_path);
    if (!dir) {
        log_formatted(LOG_ERROR, "Cannot open storage directory");
        return;
    }
    
    Message msg;
    init_message(&msg);
    msg.type = MSG_REG_SS;
    msg.ss_id = ss.id;
    strcpy(msg.sender, ss.ip);
    msg.client_port = ss.client_port;  // Use new fields - N
    printf("[SS %d] Registering with NM: IP=%s, Client Port=%d\n", 
           ss.id, ss.ip, ss.client_port);
    msg.nm_port = ss.nm_port;       // Use new fields - N
    
    struct dirent *entry;
    char file_list[MAX_BUFFER] = "";
    int file_count = 0;
    
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        if (strstr(entry->d_name, ".undo") != NULL) continue;
        
        char filepath[MAX_PATH];
        snprintf(filepath, sizeof(filepath), "%s/%s", ss.storage_path, entry->d_name);
        
        struct stat st;
        if (stat(filepath, &st) == 0 && S_ISREG(st.st_mode)) {
            if (file_count > 0) strcat(file_list, ",");
            strcat(file_list, entry->d_name);
            file_count++;
        }
    }
    closedir(dir);
    
    strcpy(msg.data, file_list);
    
    send_message(ss.nm_sock, &msg);
    log_formatted(LOG_INFO, "Registered %d files with NM", file_count);
    printf("[SS %d] Registered %d files with NM\n", ss.id, file_count);
}

void init_file_locks(const char *filename, int sentence_count) {
    if (sentence_count == 0) sentence_count = 1;
    pthread_mutex_lock(&ss.locks_mutex);
    
    for (int i = 0; i < ss.file_lock_count; i++) {
        if (strcmp(ss.file_locks[i].filename, filename) == 0) {
            if (ss.file_locks[i].lock_count < sentence_count) {
                ss.file_locks[i].locks = realloc(ss.file_locks[i].locks, 
                                                  sizeof(SentenceLock) * sentence_count);
                for (int j = ss.file_locks[i].lock_count; j < sentence_count; j++) {
                    ss.file_locks[i].locks[j].locked = 0;
                    pthread_mutex_init(&ss.file_locks[i].locks[j].mutex, NULL);
                }
                ss.file_locks[i].lock_count = sentence_count;
            }
            pthread_mutex_unlock(&ss.locks_mutex);
            return;
        }
    }
    
    strcpy(ss.file_locks[ss.file_lock_count].filename, filename);
    ss.file_locks[ss.file_lock_count].locks = malloc(sizeof(SentenceLock) * sentence_count);
    ss.file_locks[ss.file_lock_count].lock_count = sentence_count;
    
    for (int i = 0; i < sentence_count; i++) {
        ss.file_locks[ss.file_lock_count].locks[i].locked = 0;
        ss.file_locks[ss.file_lock_count].locks[i].locked_by[0] = '\0';
        pthread_mutex_init(&ss.file_locks[ss.file_lock_count].locks[i].mutex, NULL);
    }
    
    ss.file_lock_count++;
    pthread_mutex_unlock(&ss.locks_mutex);
}

SentenceLock* get_sentence_lock(const char *filename, int sentence_idx) {
    pthread_mutex_lock(&ss.locks_mutex);
    
    for (int i = 0; i < ss.file_lock_count; i++) {
        if (strcmp(ss.file_locks[i].filename, filename) == 0) {
            if (sentence_idx >= 0 && sentence_idx < ss.file_locks[i].lock_count) {
                SentenceLock *lock = &ss.file_locks[i].locks[sentence_idx];
                pthread_mutex_unlock(&ss.locks_mutex);
                return lock;
            }
            break;
        }
    }
    
    pthread_mutex_unlock(&ss.locks_mutex);
    return NULL;
}

int create_file_ss(const char *filename) {
    char filepath[MAX_PATH];
    snprintf(filepath, sizeof(filepath), "%s/%s", ss.storage_path, filename);
    
    if (access(filepath, F_OK) == 0) {
        return ERR_FILE_EXISTS;
    }
    
    FILE *file = fopen(filepath, "w");
    if (!file) {
        return ERR_SERVER_ERROR;
    }
    fclose(file);
    
    init_file_locks(filename, 1);
    
    log_formatted(LOG_INFO, "Created file: %s", filename);
    return SUCCESS;
}

int delete_file_ss(const char *filename) {
    char filepath[MAX_PATH];
    snprintf(filepath, sizeof(filepath), "%s/%s", ss.storage_path, filename);
    
    if (unlink(filepath) != 0) {
        return ERR_FILE_NOT_FOUND;
    }
    
    char undo_path[MAX_PATH];
    snprintf(undo_path, sizeof(undo_path), "%s.undo", filepath);
    unlink(undo_path);
    
    log_formatted(LOG_INFO, "Deleted file: %s", filename);
    return SUCCESS;
}

int read_file_ss(const char *filename, char *buffer) {
    char filepath[MAX_PATH];
    snprintf(filepath, sizeof(filepath), "%s/%s", ss.storage_path, filename);
    
    FILE *file = fopen(filepath, "r");
    if (!file) {
        return ERR_FILE_NOT_FOUND;
    }
    
    size_t bytes_read = fread(buffer, 1, MAX_BUFFER - 1, file);
    buffer[bytes_read] = '\0';
    
    fclose(file);
    return SUCCESS;
}

int write_file_ss(const char *filename, int sent_idx, int word_idx, const char *content) {
    char filepath[MAX_PATH];
    snprintf(filepath, sizeof(filepath), "%s/%s", ss.storage_path, filename);
    
    log_formatted(LOG_DEBUG, "Write request: file=%s, sent=%d, word=%d, content='%s'", 
                 filename, sent_idx, word_idx, content);
    
    if (create_undo_backup(filepath) != 0) {
        log_formatted(LOG_WARNING, "Could not create undo backup (file might be empty)");
    }
    
    FileContent *fc = init_file_content();
    if (parse_file(filepath, fc) != 0) {
        log_formatted(LOG_WARNING, "Could not parse file, treating as empty");
        fc->sentence_count = 1;
        fc->sentences[0].capacity = SENTENCE_CAPACITY;
        fc->sentences[0].word_count = 0;
        fc->sentences[0].words = malloc(sizeof(char*) * fc->sentences[0].capacity);
    }

    // printf("File has %d sentences before insertion\n", fc->sentence_count);
    if (fc->sentence_count == 0) {
        log_formatted(LOG_DEBUG, "File is empty, initializing with one sentence");
        fc->sentence_count = 1;
        fc->sentences[0].capacity = SENTENCE_CAPACITY;
        fc->sentences[0].word_count = 0;
        fc->sentences[0].words = malloc(sizeof(char*) * fc->sentences[0].capacity);
    }
    
    log_formatted(LOG_DEBUG, "File has %d sentences before insertion", fc->sentence_count);
    
    if (sent_idx < 0 || sent_idx > fc->sentence_count) { // Changed >= to > - S
        log_formatted(LOG_ERROR, "Invalid sentence index: %d (file has %d sentences)", 
                     sent_idx, fc->sentence_count);
        free_file_content(fc);
        return ERR_INVALID_INDEX;
    }
    
    int words_in_sentence = fc->sentences[sent_idx].word_count;
    log_formatted(LOG_DEBUG, "Sentence %d has %d words, inserting at position %d", 
                 sent_idx, words_in_sentence, word_idx);
    
    int new_sentences = insert_word_in_sentence(fc, sent_idx, word_idx, content);
    if (new_sentences < 0) {
        log_formatted(LOG_ERROR, "Failed to insert word '%s' at sentence %d, word index %d "
                     "(sentence had %d words, valid range: 1-%d)", 
                     content, sent_idx, word_idx, words_in_sentence, words_in_sentence + 1);
        free_file_content(fc);
        return ERR_INVALID_INDEX;
    }
    
    log_formatted(LOG_DEBUG, "Insertion successful, %d new sentences created, file now has %d sentences", 
                 new_sentences, fc->sentence_count);
    
    if (new_sentences > 0) {
        log_formatted(LOG_DEBUG, "Expanding locks to %d sentences", fc->sentence_count);
        init_file_locks(filename, fc->sentence_count);
    }
    
    if (write_file_content(filepath, fc) != 0) {
        log_formatted(LOG_ERROR, "Failed to write file content back to disk");
        free_file_content(fc);
        return ERR_SERVER_ERROR;
    }
    
    free_file_content(fc);
    log_formatted(LOG_INFO, "Successfully wrote to file: %s at sentence %d, word %d", 
                 filename, sent_idx, word_idx);
    return SUCCESS;
}

int lock_sentence_ss(const char *filename, int sent_idx, const char *username) {
    SentenceLock *lock = get_sentence_lock(filename, sent_idx);
    if (!lock) {
        return ERR_INVALID_INDEX;
    }
    
    pthread_mutex_lock(&lock->mutex);
    
    if (lock->locked && strcmp(lock->locked_by, username) != 0) {
        pthread_mutex_unlock(&lock->mutex);
        return ERR_SENTENCE_LOCKED;
    }
    
    lock->locked = 1;
    strncpy(lock->locked_by, username, MAX_USERNAME - 1);
    lock->lock_time = time(NULL);
    
    pthread_mutex_unlock(&lock->mutex);
    log_formatted(LOG_INFO, "Locked sentence %d in %s by %s", sent_idx, filename, username);
    return SUCCESS;
}

int unlock_sentence_ss(const char *filename, int sent_idx, const char *username) {
    SentenceLock *lock = get_sentence_lock(filename, sent_idx);
    if (!lock) {
        return ERR_INVALID_INDEX;
    }
    
    pthread_mutex_lock(&lock->mutex);
    
    if (!lock->locked || strcmp(lock->locked_by, username) != 0) {
        pthread_mutex_unlock(&lock->mutex);
        return ERR_ACCESS_DENIED;
    }
    
    lock->locked = 0;
    lock->locked_by[0] = '\0';
    
    pthread_mutex_unlock(&lock->mutex);
    log_formatted(LOG_INFO, "Unlocked sentence %d in %s by %s", sent_idx, filename, username);
    return SUCCESS;
}

int stream_file_ss(int client_sock, const char *filename) {
    char filepath[MAX_PATH];
    snprintf(filepath, sizeof(filepath), "%s/%s", ss.storage_path, filename);
    
    FileContent *fc = init_file_content();
    if (parse_file(filepath, fc) != 0) {
        free_file_content(fc);
        return ERR_FILE_NOT_FOUND;
    }
    
    Message msg;
    init_message(&msg);
    msg.type = MSG_DATA;
    msg.status = SUCCESS;
    
    for (int i = 0; i < fc->sentence_count; i++) {
        int wc = fc->sentences[i].word_count;
        for (int j = 0; j < wc; j++) {
            const char *curr = fc->sentences[i].words[j];
            const char *next = (j + 1 < wc) ? fc->sentences[i].words[j + 1] : NULL;

            /* Determine whether the server wants the client to print a trailing space.
               Rules:
                - If there is a next token in the same sentence, print a space only when
                  both current and next tokens are non-delimiters (normal word-word spacing).
                - If this is the last token of the sentence and there are more sentences,
                  request a trailing space (space between sentences).
                - Otherwise (delimiter before next word in same sentence, or last token of last sentence),
                  do not request a trailing space. - S */
            int needs_space = 0;
            if (next) {
                if (!is_delimiter(curr[0]) && !is_delimiter(next[0])) needs_space = 1;
            } else {
                if (i < fc->sentence_count - 1) needs_space = 1;
            }

            strncpy(msg.data, curr, MAX_BUFFER - 1);
            msg.data[MAX_BUFFER - 1] = '\0';
            msg.status = needs_space ? 1 : 0;
            msg.sentence_index = i;
            msg.word_index = j;
            
            if (send_message(client_sock, &msg) < 0) {
                free_file_content(fc);
                return ERR_SERVER_ERROR;
            }
            
            usleep(STREAM_DELAY);
        }
    }
    
    msg.type = MSG_STOP;
    msg.status = SUCCESS;
    send_message(client_sock, &msg);
    
    free_file_content(fc);
    return SUCCESS;
}

int get_file_info_ss(const char *filename, FileMetadata *meta) {
    char filepath[MAX_PATH];
    snprintf(filepath, sizeof(filepath), "%s/%s", ss.storage_path, filename);
    
    log_formatted(LOG_DEBUG, "Getting file info for: %s", filepath);
    
    struct stat st;
    if (stat(filepath, &st) != 0) {
        log_formatted(LOG_ERROR, "File not found: %s (errno: %d)", filepath, errno);
        return ERR_FILE_NOT_FOUND;
    }
    
    int word_count = 0, char_count = 0;
    get_file_stats(filepath, &word_count, &char_count);
    
    meta->size = st.st_size;
    meta->word_count = word_count;
    meta->char_count = char_count;
    meta->modified = st.st_mtime;
    meta->accessed = st.st_atime;
    
    log_formatted(LOG_INFO, "File info for %s: size=%zu, words=%d, chars=%d", 
                 filename, meta->size, meta->word_count, meta->char_count);
    
    return SUCCESS;
}

void* handle_client_request(void* arg) {
    int client_sock = *((int*)arg);
    free(arg);
    
    // Set timeouts for client socket
    set_socket_timeouts(client_sock, SOCKET_TIMEOUT, SOCKET_TIMEOUT);
    
    Message msg;
    
    while (ss.running) {
        if (recv_message(client_sock, &msg) < 0) {
             //log_formatted(LOG_INFO, "Client disconnected (socket=%d)", client_sock);

            //break;
            continue; // Was break before - N
        }
        
        Message response;
        init_message(&response);
        response.type = MSG_ACK;
        
        log_formatted(LOG_REQUEST, "Client request: %d for file %s", msg.type, msg.filename);
        
        switch (msg.type) {
            case MSG_READ: {
                char buffer[MAX_BUFFER];
                response.status = read_file_ss(msg.filename, buffer);
                if (response.status == SUCCESS) {
                    strncpy(response.data, buffer, MAX_BUFFER - 1);
                }
                send_message(client_sock, &response);
                break;
            }
            
            case MSG_LOCK_SENTENCE: {
                response.status = lock_sentence_ss(msg.filename, msg.sentence_index, msg.sender);
                send_message(client_sock, &response);
                break;
            }
            
            case MSG_WRITE: {
                response.status = write_file_ss(msg.filename, msg.sentence_index, 
                                               msg.word_index, msg.data);
                send_message(client_sock, &response);
                break;
            }
            
            case MSG_UNLOCK_SENTENCE: {
                response.status = unlock_sentence_ss(msg.filename, msg.sentence_index, msg.sender);
                send_message(client_sock, &response);
                break;
            }
            
            case MSG_STREAM: {
                response.status = SUCCESS;
                send_message(client_sock, &response);
                stream_file_ss(client_sock, msg.filename);
                break;
            }
            
            case MSG_UNDO: {
                char filepath[MAX_PATH];
                snprintf(filepath, sizeof(filepath), "%s/%s", ss.storage_path, msg.filename);
                
                if (undo_backup_exists(filepath)) {
                    printf("[SS DEBUG] Undo backup exists for %s\n", filepath); // Debug line - N
                    response.status = restore_from_undo(filepath);
                } else {
                    printf("[SS DEBUG] No undo backup for %s\n", filepath); // Debug line - N
                    response.status = ERR_INVALID_OPERATION;
                }
                send_message(client_sock, &response);
                break;
            }
            
            default:
                response.status = ERR_INVALID_OPERATION;
                send_message(client_sock, &response);
                break;
        }
        
        log_formatted(LOG_RESPONSE, "Response status: %d", response.status);
    }
    
    close(client_sock);
    return NULL;
}

void* client_listener(void* arg) {
    (void)arg;

    ss.client_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (ss.client_sock < 0) {
        perror("Client socket creation failed");
        return NULL;
    }
    
    int opt = 1;
    setsockopt(ss.client_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(ss.client_port);
    
    if (bind(ss.client_sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("Client socket bind failed");
        return NULL;
    }
    
    if (listen(ss.client_sock, MAX_CLIENTS) < 0) {
        perror("Client socket listen failed");
        return NULL;
    }
    
    printf("[SS %d] Listening for clients on port %d\n", ss.id, ss.client_port);
    log_formatted(LOG_INFO, "Client listener started on port %d", ss.client_port);
    
    while (ss.running) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        
        int *client_sock = malloc(sizeof(int));
        *client_sock = accept(ss.client_sock, (struct sockaddr*)&client_addr, &addr_len);
        
        if (*client_sock < 0) {
            free(client_sock);
            continue;
        }
        
        log_formatted(LOG_INFO, "Client connected from %s", inet_ntoa(client_addr.sin_addr));
        
        pthread_t tid;
        pthread_create(&tid, NULL, handle_client_request, client_sock);
        pthread_detach(tid);
    }
    
    return NULL;
}

// Heavily edited - N
void* handle_nm_communication(void* arg) {
    (void)arg;
    Message msg;
    
    // Set a reasonable timeout so we don't block forever
    struct timeval tv;
    tv.tv_sec = 1;  // 1 second timeout for receives
    tv.tv_usec = 0;
    setsockopt(ss.nm_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    
    log_formatted(LOG_INFO, "NM communication thread started");
    
    while (ss.running) {
        int recv_result = recv_message(ss.nm_sock, &msg);
        
        if (recv_result < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Timeout - this is normal, just continue
                continue;
            }
            if (errno == EINTR) {
                // Interrupted system call - just retry
                continue;
            }
            
            log_formatted(LOG_ERROR, "Lost connection to NM (errno: %d)", errno);
            ss.running = 0;
            break;
        }
        
        // Process actual commands from NM
        Message response;
        init_message(&response);
        response.type = MSG_ACK;
        response.ss_id = ss.id;
        
        log_formatted(LOG_REQUEST, "NM request: type=%d, file=%s", msg.type, msg.filename);
        
        switch (msg.type) {
            case MSG_CREATE:
                response.status = create_file_ss(msg.filename);
                log_formatted(LOG_INFO, "CREATE %s: status=%d", msg.filename, response.status);
                break;
                
            case MSG_DELETE:
                response.status = delete_file_ss(msg.filename);
                log_formatted(LOG_INFO, "DELETE %s: status=%d", msg.filename, response.status);
                break;
                
            case MSG_SS_INFO: {
                FileMetadata meta;
                memset(&meta, 0, sizeof(FileMetadata));
                
                if (strcmp(msg.data, "READ_CONTENT") == 0) {
                    char buffer[MAX_BUFFER];
                    response.status = read_file_ss(msg.filename, buffer);
                    if (response.status == SUCCESS) {
                        strncpy(response.data, buffer, MAX_BUFFER - 1);
                        response.data[MAX_BUFFER - 1] = '\0';
                        log_formatted(LOG_DEBUG, "Returning file content (%zu bytes)", strlen(buffer));
                    } else {
                        log_formatted(LOG_ERROR, "Failed to read file %s: status=%d", 
                                     msg.filename, response.status);
                    }
                } else {
                    response.status = get_file_info_ss(msg.filename, &meta);
                    if (response.status == SUCCESS) {
                        snprintf(response.data, MAX_BUFFER, "%zu|%d|%d|%ld|%ld", 
                            meta.size, meta.word_count, meta.char_count, 
                            meta.modified, meta.accessed);
                        
                        log_formatted(LOG_INFO, "Sending metadata for %s: size=%zu, words=%d, chars=%d", 
                                     msg.filename, meta.size, meta.word_count, meta.char_count);
                    } else {
                        log_formatted(LOG_ERROR, "Failed to get file info for %s: status=%d", 
                                     msg.filename, response.status);
                    }
                }
                break;
            }
            
            default:
                log_formatted(LOG_WARNING, "Unknown message type from NM: %d", msg.type);
                response.status = ERR_INVALID_OPERATION;
                break;
        }
        
        // NEW: No mutex needed, dedicated socket
        int send_result = send_message(ss.nm_sock, &response);
        
        if (send_result < 0) {
            log_formatted(LOG_ERROR, "Failed to send response to NM (errno: %d)", errno);
            if (errno == EPIPE || errno == ECONNRESET || errno == ENOTCONN) {
                log_formatted(LOG_ERROR, "Connection to NM broken, shutting down");
                ss.running = 0;
                break;
            }
        } else {
            log_formatted(LOG_RESPONSE, "Sent response to NM: status=%d", response.status);
        }
    }
    
    log_formatted(LOG_INFO, "NM communication thread exiting");
    return NULL;
}

// Edited to handle heartbeat socket, checking alive connections and closing if they are not - N
void* heartbeat_thread(void* arg) {
    (void)arg;
    
    log_formatted(LOG_INFO, "Heartbeat thread started");
    
    // Send initial identification on heartbeat socket - N
    Message ident;
    init_message(&ident);
    ident.type = MSG_ACK;
    ident.ss_id = ss.id;
    strcpy(ident.data, "HB_INIT");
    send_message(ss.nm_hb_sock, &ident);
    
    while (ss.running) {
        sleep(HEARTBEAT_INTERVAL);
        
        Message msg;
        init_message(&msg);
        msg.type = MSG_ACK;
        msg.ss_id = ss.id;
        strcpy(msg.data, "HEARTBEAT");
        
        log_formatted(LOG_DEBUG, "Sending heartbeat to NM");
        
        // Use heartbeat socket, no mutex needed - N
        int result = send_message(ss.nm_hb_sock, &msg);
        
        // Kill off the connection if we get errors indicating it's dead - N
        if (result < 0) {
            if (errno == EPIPE || errno == ECONNRESET || errno == ENOTCONN) {
                log_formatted(LOG_ERROR, "Connection lost to NM (errno: %d)", errno);
                ss.running = 0;
                break;
            }
            log_formatted(LOG_WARNING, "Heartbeat send failed (errno: %d), will retry", errno);
        } else {
            log_formatted(LOG_DEBUG, "Heartbeat sent successfully");
        }
    }
    
    log_formatted(LOG_INFO, "Heartbeat thread exiting");
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc != 5) {
        printf("Usage: %s <nm_ip> <nm_port> <client_port> <dir_name>\n", argv[0]);
        return 1;
    }
    
    char *nm_ip = argv[1];
    int nm_port = atoi(argv[2]);
    int client_port = atoi(argv[3]);
    int ss_id = atoi(argv[4]);

    // ADD: Validate ports
    if (nm_port <= 0 || nm_port > 65535) {
        printf("Error: Invalid NM port number. Must be between 1 and 65535.\n");
        return 1;
    }
    
    if (client_port <= 0 || client_port > 65535) {
        printf("Error: Invalid client port number. Must be between 1 and 65535.\n");
        return 1;
    }
    
    if (client_port == nm_port) {
        printf("Error: Client port and NM port cannot be the same.\n");
        return 1;
    }
    
    // Warn if not using standard ports (helpful for debugging)
    if (nm_port != NM_SS_PORT) {
        printf("[SS] Warning: Connecting to NM on non-standard port %d (expected %d)\n", 
               nm_port, NM_SS_PORT);
    }
    
    init_storage_server(nm_ip, nm_port, client_port, ss_id);
    connect_to_nm(nm_ip, nm_port);
    scan_and_register_files();
    
    pthread_t nm_thread, client_thread, hb_thread;
    
    // Robust checking - N
    if (pthread_create(&nm_thread, NULL, handle_nm_communication, NULL) != 0) {
        log_formatted(LOG_ERROR, "Failed to create NM thread");
        return 1;
    }
    
    if (pthread_create(&client_thread, NULL, client_listener, NULL) != 0) {
        log_formatted(LOG_ERROR, "Failed to create client thread");
        return 1;
    }

    sleep(1); // Ensure NM thread is up before heartbeat thread starts
    
    if (pthread_create(&hb_thread, NULL, heartbeat_thread, NULL) != 0) {
        log_formatted(LOG_ERROR, "Failed to create heartbeat thread");
        return 1;
    }
    
    printf("[SS %d] Storage Server running. Press Ctrl+C to stop.\n", ss.id);
    log_formatted(LOG_INFO, "All threads started successfully");
    
    pthread_join(nm_thread, NULL);
    pthread_join(client_thread, NULL);
    pthread_join(hb_thread, NULL);
    
    close(ss.nm_sock);
    close(ss.nm_hb_sock);
    close(ss.client_sock);
    close_logger();
    
    return 0;
}