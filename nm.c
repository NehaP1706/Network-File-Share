#include "common.h"
#include "logger.h"
#include "trie.h"
#include "cache.h"

#define NM_SS_PORT 8080
#define NM_CLIENT_PORT 8081
#define HEARTBEAT_TIMEOUT 15

typedef struct {
    Trie *file_trie;
    LRUCache *cache;
    
    StorageServerInfo ss_list[MAX_SS];
    int ss_count;
    pthread_mutex_t ss_mutex;
    int next_ss_id;
    
    ClientInfo client_list[MAX_CLIENTS];
    int client_count;
    pthread_mutex_t client_mutex;
    
    int ss_sock;
    int client_sock;
    volatile int running;
} NameServer;

NameServer nm;

// Function declarations
void* handle_ss_connection(void* arg);
void* handle_client_connection(void* arg);
void* ss_listener(void* arg);
void* client_listener(void* arg);
void* heartbeat_monitor(void* arg);
int find_ss_for_file(const char *filename);
int get_next_ss_round_robin();
void handle_view(int client_sock, Message *msg);
void handle_info(int client_sock, Message *msg);
void handle_list(int client_sock, Message *msg);
void handle_create(int client_sock, Message *msg);
void handle_delete(int client_sock, Message *msg);
void handle_access(int client_sock, Message *msg);
void handle_exec(int client_sock, Message *msg);
int check_access(const char *filename, const char *username, AccessType required);

void init_name_server() {
    nm.file_trie = init_trie();
    nm.cache = init_cache(CACHE_SIZE);
    nm.ss_count = 0;
    nm.client_count = 0;
    nm.next_ss_id = 0;
    nm.running = 1;
    
    pthread_mutex_init(&nm.ss_mutex, NULL);
    pthread_mutex_init(&nm.client_mutex, NULL);
    
    set_instance_name("NM");  // ADD THIS LINE
    init_logger("nm.log");
    
    printf("[NM] Name Server initialized\n");
    printf("[NM] SS Port: %d\n", NM_SS_PORT);
    printf("[NM] Client Port: %d\n", NM_CLIENT_PORT);
}

int find_ss_for_file(const char *filename) {
    FileMetadata *meta = cache_get(nm.cache, filename);
    
    if (!meta) {
        meta = trie_search(nm.file_trie, filename);
        if (meta) {
            cache_put(nm.cache, filename, meta);
        }
    }
    
    if (meta) {
        int ss_id = meta->ss_id;
        free(meta);
        return ss_id;
    }
    
    return -1;
}

int get_next_ss_round_robin() {
    pthread_mutex_lock(&nm.ss_mutex);
    
    if (nm.ss_count == 0) {
        pthread_mutex_unlock(&nm.ss_mutex);
        return -1;
    }
    
    int ss_id = nm.next_ss_id % nm.ss_count;
    nm.next_ss_id++;
    
    pthread_mutex_unlock(&nm.ss_mutex);
    return nm.ss_list[ss_id].id;
}

int check_access(const char *filename, const char *username, AccessType required) {
    FileMetadata *meta = trie_search(nm.file_trie, filename);
    if (!meta) return 0;
    
    // Owner has all access
    if (strcmp(meta->owner, username) == 0) {
        free(meta);
        return 1;
    }
    
    // Check ACL
    for (int i = 0; i < meta->acl_count; i++) {
        if (strcmp(meta->acl[i].username, username) == 0) {
            int has_access = 0;
            if (required == ACCESS_READ && 
                (meta->acl[i].access == ACCESS_READ || meta->acl[i].access == ACCESS_READWRITE)) {
                has_access = 1;
            } else if (required == ACCESS_WRITE && meta->acl[i].access == ACCESS_READWRITE) {
                has_access = 1;
            }
            free(meta);
            return has_access;
        }
    }
    
    free(meta);
    return 0;
}

void handle_view(int client_sock, Message *msg) {
    int show_all = (strstr(msg->data, "-a") != NULL);
    int show_details = (strstr(msg->data, "-l") != NULL);
    
    FileMetadata *files[MAX_FILES];
    int file_count = trie_get_all_files(nm.file_trie, files, MAX_FILES);
    
    Message response;
    init_message(&response);
    response.type = MSG_DATA;
    response.status = SUCCESS;
    
    char buffer[MAX_BUFFER];
    int pos = 0;
    
    if (show_details) {
        pos += sprintf(buffer + pos, "%-20s %-8s %-8s %-20s %-10s\n", 
                      "Filename", "Words", "Chars", "Last Access", "Owner");
        pos += sprintf(buffer + pos, "%s\n", 
                      "--------------------------------------------------------------------------------");
    }
    
    for (int i = 0; i < file_count; i++) {
        int has_access = show_all || check_access(files[i]->filename, msg->sender, ACCESS_READ);
        
        if (has_access) {
            if (show_details) {
                char time_str[32];
                struct tm *tm_info = localtime(&files[i]->accessed); //changed localtime_r to localtime - S
                strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);
                
                pos += sprintf(buffer + pos, "%-20s %-8d %-8d %-20s %-10s\n",
                              files[i]->filename, files[i]->word_count, 
                              files[i]->char_count, time_str, files[i]->owner);
            } else {
                pos += sprintf(buffer + pos, "%s\n", files[i]->filename);
            }
        }
        free(files[i]);
    }
    
    strncpy(response.data, buffer, MAX_BUFFER - 1);
    send_message(client_sock, &response);
    
    log_formatted(LOG_INFO, "VIEW request from %s: %d files", msg->sender, file_count);
}

void handle_info(int client_sock, Message *msg) {
    FileMetadata *meta = trie_search(nm.file_trie, msg->filename);
    
    Message response;
    init_message(&response);
    response.type = MSG_DATA;
    
    if (!meta) {
        response.status = ERR_FILE_NOT_FOUND;
        send_message(client_sock, &response);
        return;
    }
    
    // Get updated info from SS
    int ss_id = find_ss_for_file(msg->filename);
    if (ss_id >= 0) {
        pthread_mutex_lock(&nm.ss_mutex);
        for (int i = 0; i < nm.ss_count; i++) {
            if (nm.ss_list[i].id == ss_id) {
                Message ss_req;
                init_message(&ss_req);
                ss_req.type = MSG_SS_INFO;
                strcpy(ss_req.filename, msg->filename);
                
                send_message(nm.ss_list[i].sock, &ss_req);
                
                Message ss_resp;
                if (recv_message(nm.ss_list[i].sock, &ss_resp) == 0 && 
                    ss_resp.status == SUCCESS) {
                    sscanf(ss_resp.data, "%zu|%d|%d|%ld|%ld",
                           &meta->size, &meta->word_count, &meta->char_count,
                           &meta->modified, &meta->accessed);
                }
                break;
            }
        }
        pthread_mutex_unlock(&nm.ss_mutex);
    }
    
    char buffer[MAX_BUFFER];
    char created_str[32], modified_str[32], accessed_str[32];
    struct tm *tm_info;
    
    tm_info = localtime(&meta->created); //changed localtime_r to localtime - S
    strftime(created_str, sizeof(created_str), "%Y-%m-%d %H:%M:%S", tm_info);
    tm_info = localtime(&meta->modified); //changed localtime_r to localtime - S
    strftime(modified_str, sizeof(modified_str), "%Y-%m-%d %H:%M:%S", tm_info);
    tm_info = localtime(&meta->accessed); //changed localtime_r to localtime - S
    strftime(accessed_str, sizeof(accessed_str), "%Y-%m-%d %H:%M:%S", tm_info);
    
    sprintf(buffer, "File: %s\nOwner: %s\nCreated: %s\nLast Modified: %s\n"
                    "Last Accessed: %s by %s\nSize: %zu bytes\nWords: %d\nChars: %d\n"
                    "Storage Server: %d\nAccess Control:\n",
            meta->filename, meta->owner, created_str, modified_str, 
            accessed_str, meta->last_accessed_by, meta->size, 
            meta->word_count, meta->char_count, meta->ss_id);
    
    for (int i = 0; i < meta->acl_count; i++) {
        char access_str[10];
        if (meta->acl[i].access == ACCESS_READ) strcpy(access_str, "R");
        else if (meta->acl[i].access == ACCESS_WRITE) strcpy(access_str, "W");
        else if (meta->acl[i].access == ACCESS_READWRITE) strcpy(access_str, "RW");
        else strcpy(access_str, "NONE");
        
        sprintf(buffer + strlen(buffer), "  %s: %s\n", meta->acl[i].username, access_str);
    }
    
    strncpy(response.data, buffer, MAX_BUFFER - 1);
    response.status = SUCCESS;
    send_message(client_sock, &response);
    
    free(meta);
    log_formatted(LOG_INFO, "INFO request for %s from %s", msg->filename, msg->sender);
}

void handle_list(int client_sock, Message *msg) {
    Message response;
    init_message(&response);
    response.type = MSG_DATA;
    response.status = SUCCESS;
    
    char buffer[MAX_BUFFER] = "";
    
    pthread_mutex_lock(&nm.client_mutex);
    for (int i = 0; i < nm.client_count; i++) {
        strcat(buffer, nm.client_list[i].username);
        strcat(buffer, "\n");
    }
    pthread_mutex_unlock(&nm.client_mutex);
    
    strncpy(response.data, buffer, MAX_BUFFER - 1);
    send_message(client_sock, &response);
    
    log_formatted(LOG_INFO, "LIST request from %s", msg->sender);
}

void handle_create(int client_sock, Message *msg) {
    Message response;
    init_message(&response);
    
    // Check if file already exists
    FileMetadata *existing = trie_search(nm.file_trie, msg->filename);
    if (existing) {
        free(existing);
        response.status = ERR_FILE_EXISTS;
        send_message(client_sock, &response);
        // or we could not error out an just continue?
        return;
    }
    
    // Get SS to store file (round-robin)
    int ss_id = get_next_ss_round_robin();
    if (ss_id < 0) {
        response.status = ERR_SS_UNAVAILABLE;
        send_message(client_sock, &response);
        return;
    }
    
    // Forward create request to SS
    pthread_mutex_lock(&nm.ss_mutex);
    int ss_idx = -1;
    for (int i = 0; i < nm.ss_count; i++) {
        if (nm.ss_list[i].id == ss_id) {
            ss_idx = i;
            break;
        }
    }
    
    if (ss_idx < 0) {
        pthread_mutex_unlock(&nm.ss_mutex);
        response.status = ERR_SS_UNAVAILABLE;
        send_message(client_sock, &response);
        return;
    }
    
    Message ss_msg;
    init_message(&ss_msg);
    ss_msg.type = MSG_CREATE;
    strcpy(ss_msg.filename, msg->filename);
    
    send_message(nm.ss_list[ss_idx].sock, &ss_msg);
    
    Message ss_response;
    recv_message(nm.ss_list[ss_idx].sock, &ss_response);
    pthread_mutex_unlock(&nm.ss_mutex);
    
    if (ss_response.status == SUCCESS) {
        // Add to trie
        FileMetadata meta;
        memset(&meta, 0, sizeof(FileMetadata));
        strcpy(meta.filename, msg->filename);
        strcpy(meta.owner, msg->sender);
        meta.ss_id = ss_id;
        meta.created = time(NULL);
        meta.modified = meta.created;
        meta.accessed = meta.created;
        strcpy(meta.last_accessed_by, msg->sender);
        meta.acl_count = 0;
        
        trie_insert(nm.file_trie, msg->filename, &meta);
        cache_put(nm.cache, msg->filename, &meta);
        
        response.status = SUCCESS;
        log_formatted(LOG_INFO, "Created file %s by %s on SS %d", 
                     msg->filename, msg->sender, ss_id);
    } else {
        response.status = ss_response.status;
    }
    
    send_message(client_sock, &response);
}

void handle_delete(int client_sock, Message *msg) {
    Message response;
    init_message(&response);
    
    FileMetadata *meta = trie_search(nm.file_trie, msg->filename);
    if (!meta) {
        response.status = ERR_FILE_NOT_FOUND;
        send_message(client_sock, &response);
        return;
    }
    
    // Check if user is owner
    if (strcmp(meta->owner, msg->sender) != 0) {
        free(meta);
        response.status = ERR_NOT_OWNER;
        send_message(client_sock, &response);
        return;
    }
    
    int ss_id = meta->ss_id;
    free(meta);
    
    // Forward delete to SS
    pthread_mutex_lock(&nm.ss_mutex);
    int ss_idx = -1;
    for (int i = 0; i < nm.ss_count; i++) {
        if (nm.ss_list[i].id == ss_id) {
            ss_idx = i;
            break;
        }
    }
    
    if (ss_idx < 0) {
        pthread_mutex_unlock(&nm.ss_mutex);
        response.status = ERR_SS_UNAVAILABLE;
        send_message(client_sock, &response);
        return;
    }
    
    Message ss_msg;
    init_message(&ss_msg);
    ss_msg.type = MSG_DELETE;
    strcpy(ss_msg.filename, msg->filename);
    
    send_message(nm.ss_list[ss_idx].sock, &ss_msg);
    
    Message ss_response;
    recv_message(nm.ss_list[ss_idx].sock, &ss_response);
    pthread_mutex_unlock(&nm.ss_mutex);
    
    if (ss_response.status == SUCCESS) {
        trie_delete(nm.file_trie, msg->filename);
        cache_remove(nm.cache, msg->filename);
        response.status = SUCCESS;
        log_formatted(LOG_INFO, "Deleted file %s by %s", msg->filename, msg->sender);
    } else {
        response.status = ss_response.status;
    }
    
    send_message(client_sock, &response);
}

void handle_access(int client_sock, Message *msg) {
    Message response;
    init_message(&response);
    
    FileMetadata *meta = trie_search(nm.file_trie, msg->filename);
    if (!meta) {
        response.status = ERR_FILE_NOT_FOUND;
        send_message(client_sock, &response);
        return;
    }
    
    // Check if user is owner
    if (strcmp(meta->owner, msg->sender) != 0) {
        free(meta);
        response.status = ERR_NOT_OWNER;
        send_message(client_sock, &response);
        return;
    }
    
    if (msg->type == MSG_ADDACCESS) {
        // Check if user already has access
        int found = 0;
        for (int i = 0; i < meta->acl_count; i++) {
            if (strcmp(meta->acl[i].username, msg->target_user) == 0) {
                meta->acl[i].access = msg->access;
                found = 1;
                break;
            }
        }
        
        if (!found && meta->acl_count < MAX_ACL_ENTRIES) {
            strcpy(meta->acl[meta->acl_count].username, msg->target_user);
            meta->acl[meta->acl_count].access = msg->access;
            meta->acl_count++;
        }
        
        trie_update(nm.file_trie, msg->filename, meta);
        cache_put(nm.cache, msg->filename, meta);
        response.status = SUCCESS;
        
        log_formatted(LOG_INFO, "Added access for %s to %s (access: %d)", 
                     msg->target_user, msg->filename, msg->access);
        
    } else if (msg->type == MSG_REMACCESS) {
        // Remove access
        for (int i = 0; i < meta->acl_count; i++) {
            if (strcmp(meta->acl[i].username, msg->target_user) == 0) {
                // Shift remaining entries
                for (int j = i; j < meta->acl_count - 1; j++) {
                    meta->acl[j] = meta->acl[j + 1];
                }
                meta->acl_count--;
                break;
            }
        }
        
        trie_update(nm.file_trie, msg->filename, meta);
        cache_put(nm.cache, msg->filename, meta);
        response.status = SUCCESS;
        
        log_formatted(LOG_INFO, "Removed access for %s from %s", 
                     msg->target_user, msg->filename);
    }
    
    free(meta);
    send_message(client_sock, &response);
}

void handle_exec(int client_sock, Message *msg) {
    Message response;
    init_message(&response);
    
    // Check read access
    if (!check_access(msg->filename, msg->sender, ACCESS_READ)) {
        response.status = ERR_ACCESS_DENIED;
        send_message(client_sock, &response);
        return;
    }
    
    // Get file content from SS
    int ss_id = find_ss_for_file(msg->filename);
    if (ss_id < 0) {
        response.status = ERR_FILE_NOT_FOUND;
        send_message(client_sock, &response);
        return;
    }
    
    pthread_mutex_lock(&nm.ss_mutex);
    int ss_idx = -1;
    for (int i = 0; i < nm.ss_count; i++) {
        if (nm.ss_list[i].id == ss_id) {
            ss_idx = i;
            break;
        }
    }
    
    if (ss_idx < 0) {
        pthread_mutex_unlock(&nm.ss_mutex);
        response.status = ERR_SS_UNAVAILABLE;
        send_message(client_sock, &response);
        return;
    }
    
    Message ss_msg;
    init_message(&ss_msg);
    ss_msg.type = MSG_SS_INFO;
    strcpy(ss_msg.filename, msg->filename);
    strcpy(ss_msg.data, "READ_CONTENT");
    
    send_message(nm.ss_list[ss_idx].sock, &ss_msg);
    
    Message ss_response;
    recv_message(nm.ss_list[ss_idx].sock, &ss_response);
    pthread_mutex_unlock(&nm.ss_mutex);
    
    if (ss_response.status != SUCCESS) {
        response.status = ss_response.status;
        send_message(client_sock, &response);
        return;
    }
    
    // Execute commands
    FILE *fp = popen(ss_response.data, "r");
    if (!fp) {
        response.status = ERR_SERVER_ERROR;
        send_message(client_sock, &response);
        return;
    }
    
    char buffer[MAX_BUFFER];
    size_t bytes_read = fread(buffer, 1, MAX_BUFFER - 1, fp);
    buffer[bytes_read] = '\0';
    pclose(fp);
    
    strncpy(response.data, buffer, MAX_BUFFER - 1);
    response.status = SUCCESS;
    send_message(client_sock, &response);
    
    log_formatted(LOG_INFO, "Executed file %s for %s", msg->filename, msg->sender);
}

void* handle_ss_connection(void* arg) {
    int ss_sock = *((int*)arg);
    free(arg);
    
    Message msg;
    if (recv_message(ss_sock, &msg) < 0 || msg.type != MSG_REG_SS) {
        close(ss_sock);
        return NULL;
    }
    
    pthread_mutex_lock(&nm.ss_mutex);
    
    if (nm.ss_count >= MAX_SS) {
        pthread_mutex_unlock(&nm.ss_mutex);
        close(ss_sock);
        return NULL;
    }
    
    int idx = nm.ss_count;
    nm.ss_list[idx].id = msg.ss_id;
    strcpy(nm.ss_list[idx].ip, msg.sender);
    nm.ss_list[idx].nm_port = msg.word_index; //sus
    nm.ss_list[idx].client_port = msg.sentence_index; //sus
    nm.ss_list[idx].sock = ss_sock;
    nm.ss_list[idx].active = 1;
    nm.ss_list[idx].last_heartbeat = time(NULL);
    nm.ss_list[idx].file_count = 0;
    
    // Parse file list
    char *file_list = strdup(msg.data);
    char *token = strtok(file_list, ",");
    while (token && nm.ss_list[idx].file_count < MAX_FILES) {
        strcpy(nm.ss_list[idx].files[nm.ss_list[idx].file_count], token);
        
        // Add to trie
        FileMetadata meta;
        memset(&meta, 0, sizeof(FileMetadata));
        strcpy(meta.filename, token);
        meta.ss_id = msg.ss_id;
        strcpy(meta.owner, "system");
        meta.created = time(NULL);
        meta.modified = meta.created;
        meta.accessed = meta.created;
        
        trie_insert(nm.file_trie, token, &meta);
        
        nm.ss_list[idx].file_count++;
        token = strtok(NULL, ",");
    }
    free(file_list);
    
    nm.ss_count++;
    pthread_mutex_unlock(&nm.ss_mutex);
    
    log_formatted(LOG_INFO, "SS %d registered with %d files", 
                 msg.ss_id, nm.ss_list[idx].file_count);
    printf("[NM] Storage Server %d connected from %s\n", msg.ss_id, msg.sender);
    
    // Handle ongoing communication (mainly heartbeats)
    while (nm.running) {
        if (recv_message(ss_sock, &msg) < 0) {
            break;
        }
        
        if (msg.type == MSG_ACK && strcmp(msg.data, "HEARTBEAT") == 0) {
            pthread_mutex_lock(&nm.ss_mutex);
            for (int i = 0; i < nm.ss_count; i++) {
                if (nm.ss_list[i].sock == ss_sock) {
                    nm.ss_list[i].last_heartbeat = time(NULL);
                    log_formatted(LOG_DEBUG, "Heartbeat received from SS %d", 
                                 nm.ss_list[i].id);
                    break;
                }
            }
            pthread_mutex_unlock(&nm.ss_mutex);
            // Don't send response - heartbeats are one-way
        }
    }

    pthread_mutex_lock(&nm.ss_mutex);
    for (int i = 0; i < nm.ss_count; i++) {
        if (nm.ss_list[i].sock == ss_sock) {
            nm.ss_list[i].active = 0;
            log_formatted(LOG_WARNING, "SS %d disconnected", nm.ss_list[i].id);
            break;
        }
    }
    pthread_mutex_unlock(&nm.ss_mutex);
    
    close(ss_sock);
    return NULL;
}

void* handle_client_connection(void* arg) {
    int client_sock = *((int*)arg);
    free(arg);
    
    Message msg;
    if (recv_message(client_sock, &msg) < 0 || msg.type != MSG_REG_CLIENT) {
        close(client_sock);
        return NULL;
    }
    
    pthread_mutex_lock(&nm.client_mutex);
    
    if (nm.client_count >= MAX_CLIENTS) {
        pthread_mutex_unlock(&nm.client_mutex);
        close(client_sock);
        return NULL;
    }
    
    int idx = nm.client_count;
    strcpy(nm.client_list[idx].username, msg.sender);
    strcpy(nm.client_list[idx].ip, msg.data);
    nm.client_list[idx].sock = client_sock;
    nm.client_list[idx].connected = time(NULL);
    nm.client_count++;
    
    pthread_mutex_unlock(&nm.client_mutex);
    
    log_formatted(LOG_INFO, "Client %s connected from %s", msg.sender, msg.data);
    printf("[NM] Client %s connected\n", msg.sender);
    
    // Send ACK
    Message response;
    init_message(&response);
    response.status = SUCCESS;
    send_message(client_sock, &response);
    
    // Handle client requests
    while (nm.running) {
        if (recv_message(client_sock, &msg) < 0) {
            break;
        }
        
        log_formatted(LOG_REQUEST, "Request from %s: type=%d, file=%s", 
                     msg.sender, msg.type, msg.filename);
        
        switch (msg.type) {
            case MSG_VIEW:
                handle_view(client_sock, &msg);
                break;
            case MSG_INFO:
                handle_info(client_sock, &msg);
                break;
            case MSG_LIST:
                handle_list(client_sock, &msg);
                break;
            case MSG_CREATE:
                handle_create(client_sock, &msg);
                break;
            case MSG_DELETE:
                handle_delete(client_sock, &msg);
                break;
            case MSG_ADDACCESS:
            case MSG_REMACCESS:
                handle_access(client_sock, &msg);
                break;
            case MSG_EXEC:
                handle_exec(client_sock, &msg);
                break;
            case MSG_READ:
            case MSG_WRITE:
            case MSG_STREAM:
            case MSG_UNDO: {
                // Return SS info for direct connection
                response.type = MSG_DATA;
                
                if (msg.type == MSG_WRITE) {
                    if (!check_access(msg.filename, msg.sender, ACCESS_WRITE)) {
                        response.status = ERR_ACCESS_DENIED;
                        send_message(client_sock, &response);
                        break;
                    }
                } else {
                    if (!check_access(msg.filename, msg.sender, ACCESS_READ)) {
                        response.status = ERR_ACCESS_DENIED;
                        send_message(client_sock, &response);
                        break;
                    }
                }
                
                int ss_id = find_ss_for_file(msg.filename);
                if (ss_id < 0) {
                    response.status = ERR_FILE_NOT_FOUND;
                    send_message(client_sock, &response);
                    break;
                }
                
                pthread_mutex_lock(&nm.ss_mutex);
                for (int i = 0; i < nm.ss_count; i++) {
                    if (nm.ss_list[i].id == ss_id) {
                        sprintf(response.data, "%s:%d", 
                               nm.ss_list[i].ip, nm.ss_list[i].client_port);
                        response.status = SUCCESS;
                        break;
                    }
                }
                pthread_mutex_unlock(&nm.ss_mutex);
                
                send_message(client_sock, &response);
                break;
            }
            default:
                response.status = ERR_INVALID_OPERATION;
                send_message(client_sock, &response);
                break;
        }
    }
    
    pthread_mutex_lock(&nm.client_mutex);
    for (int i = 0; i < nm.client_count; i++) {
        if (nm.client_list[i].sock == client_sock) {
            for (int j = i; j < nm.client_count - 1; j++) {
                nm.client_list[j] = nm.client_list[j + 1];
            }
            nm.client_count--;
            break;
        }
    }
    pthread_mutex_unlock(&nm.client_mutex);
    
    close(client_sock);
    return NULL;
}

void* ss_listener(void* arg) {
    (void) arg;

    nm.ss_sock = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(nm.ss_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(NM_SS_PORT);
    
    bind(nm.ss_sock, (struct sockaddr*)&addr, sizeof(addr));
    listen(nm.ss_sock, MAX_SS);
    
    printf("[NM] Listening for Storage Servers on port %d\n", NM_SS_PORT);
    
    while (nm.running) {
        int *ss_sock = malloc(sizeof(int));
        *ss_sock = accept(nm.ss_sock, NULL, NULL);
        
        if (*ss_sock < 0) {
            free(ss_sock);
            continue;
        }
        
        pthread_t tid;
        pthread_create(&tid, NULL, handle_ss_connection, ss_sock);
        pthread_detach(tid);
    }
    
    return NULL;
}

void* client_listener(void* arg) {
    (void) arg;
    nm.client_sock = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(nm.client_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(NM_CLIENT_PORT);
    
    bind(nm.client_sock, (struct sockaddr*)&addr, sizeof(addr));
    listen(nm.client_sock, MAX_CLIENTS);
    
    printf("[NM] Listening for Clients on port %d\n", NM_CLIENT_PORT);
    
    while (nm.running) {
        int *client_sock = malloc(sizeof(int));
        *client_sock = accept(nm.client_sock, NULL, NULL);
        
        if (*client_sock < 0) {
            free(client_sock);
            continue;
        }
        
        pthread_t tid;
        pthread_create(&tid, NULL, handle_client_connection, client_sock);
        pthread_detach(tid);
    }
    
    return NULL;
}

void* heartbeat_monitor(void* arg) {
    (void) arg;
    
    while (nm.running) {
        sleep(5);
        
        time_t now = time(NULL);
        pthread_mutex_lock(&nm.ss_mutex);
        
        for (int i = 0; i < nm.ss_count; i++) {
            if (now - nm.ss_list[i].last_heartbeat > HEARTBEAT_TIMEOUT) {
                log_formatted(LOG_WARNING, "SS %d heartbeat timeout", nm.ss_list[i].id);
                nm.ss_list[i].active = 0;
            }
        }
        
        pthread_mutex_unlock(&nm.ss_mutex);
    }
    
    return NULL;
}

int main() {
    init_name_server();
    
    pthread_t ss_thread, client_thread, hb_thread;
    pthread_create(&ss_thread, NULL, ss_listener, NULL);
    pthread_create(&client_thread, NULL, client_listener, NULL);
    pthread_create(&hb_thread, NULL, heartbeat_monitor, NULL);
    
    printf("[NM] Name Server running. Press Ctrl+C to stop.\n");
    
    pthread_join(ss_thread, NULL);
    pthread_join(client_thread, NULL);
    pthread_join(hb_thread, NULL);
    
    free_trie(nm.file_trie);
    free_cache(nm.cache);
    close_logger();
    
    return 0;
}
