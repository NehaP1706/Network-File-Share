
#include "common.h"
#include "logger.h"
#include "trie.h"
#include "cache.h"
#include <ctype.h>
#include <sys/time.h>

// #define NM_SS_PORT 8080
// #define NM_CLIENT_PORT 8081
#define HEARTBEAT_TIMEOUT 15

typedef struct {
    Trie *file_trie;
    FolderTrie *folder_trie;
    LRUCache *cache;
    
    StorageServerInfo ss_list[MAX_SS];
    int ss_count;
    pthread_mutex_t ss_mutex;
    pthread_mutex_t ss_sock_mutexes[MAX_SS];  // One mutex per SS socket - N
    int next_ss_id;
    
    ClientInfo client_list[MAX_CLIENTS];
    int client_count;
    pthread_mutex_t client_mutex;
    
    int ss_sock;
    int ss_hb_sock;            // heartbeat listener - N
    int client_sock;
    volatile int running;
} NameServer;

NameServer nm;

// Function declarations (same as before)
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

// Added new function declarations for heartbeat handling - N
void* ss_hb_listener(void* arg);
void* handle_ss_heartbeat(void* arg);

void init_name_server() {
    nm.file_trie = init_trie();
    nm.folder_trie = init_folder_trie();
    nm.cache = init_cache(CACHE_SIZE);
    nm.ss_count = 0;
    nm.client_count = 0;
    nm.next_ss_id = 0;
    nm.running = 1;
    
    pthread_mutex_init(&nm.ss_mutex, NULL);
    pthread_mutex_init(&nm.client_mutex, NULL);
    
    // ADD: Initialize per-SS socket mutexes - N
    for (int i = 0; i < MAX_SS; i++) {
        pthread_mutex_init(&nm.ss_sock_mutexes[i], NULL);
        nm.ss_list[i].hb_sock = -1;  // Initialize all to -1, set later - N
    }
    
    set_instance_name("NM");
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

void handle_createfolder(int client_sock, Message *msg) {
    Message response;
    init_message(&response);
    
    // Build full path
    char full_path[MAX_PATH];
    if (strlen(msg->target_path) > 0) {
        snprintf(full_path, sizeof(full_path), "%s/%s", msg->target_path, msg->foldername);
    } else {
        snprintf(full_path, sizeof(full_path), "/%s", msg->foldername);
    }
    
    // Check if folder exists
    FolderMetadata *existing = folder_trie_search(nm.folder_trie, full_path);
    if (existing) {
        free(existing);
        response.status = ERR_FILE_EXISTS;
        send_message(client_sock, &response);
        return;
    }
    
    // Get SS for folder
    int ss_id = get_next_ss_round_robin();
    if (ss_id < 0) {
        response.status = ERR_SS_UNAVAILABLE;
        send_message(client_sock, &response);
        return;
    }
    
    // Forward to SS
    pthread_mutex_lock(&nm.ss_mutex);
    int ss_idx = -1;
    for (int i = 0; i < nm.ss_count; i++) {
        if (nm.ss_list[i].id == ss_id && nm.ss_list[i].active) {
            ss_idx = i;
            break;
        }
    }
    pthread_mutex_unlock(&nm.ss_mutex);
    
    if (ss_idx < 0) {
        response.status = ERR_SS_UNAVAILABLE;
        send_message(client_sock, &response);
        return;
    }
    
    Message ss_msg;
    init_message(&ss_msg);
    ss_msg.type = MSG_CREATEFOLDER;
    strcpy(ss_msg.foldername, msg->foldername);
    strcpy(ss_msg.target_path, full_path);
    
    pthread_mutex_lock(&nm.ss_sock_mutexes[ss_idx]);
    send_message(nm.ss_list[ss_idx].sock, &ss_msg);
    
    Message ss_response;
    recv_message(nm.ss_list[ss_idx].sock, &ss_response);
    pthread_mutex_unlock(&nm.ss_sock_mutexes[ss_idx]);
    
    if (ss_response.status == SUCCESS) {
        // Add to folder trie
        FolderMetadata folder_meta;
        memset(&folder_meta, 0, sizeof(FolderMetadata));
        strcpy(folder_meta.foldername, msg->foldername);
        strcpy(folder_meta.parent_path, msg->target_path);
        strcpy(folder_meta.owner, msg->sender);
        folder_meta.created = time(NULL);
        folder_meta.ss_id = ss_id;
        folder_meta.acl_count = 0;
        
        folder_trie_insert(nm.folder_trie, full_path, &folder_meta);
        response.status = SUCCESS;
        log_formatted(LOG_INFO, "Created folder %s by %s on SS %d", 
                     full_path, msg->sender, ss_id);
    } else {
        response.status = ss_response.status;
    }
    
    send_message(client_sock, &response);
}

void handle_move(int client_sock, Message *msg) {
    Message response;
    init_message(&response);
    
    // Check if file exists
    FileMetadata *file_meta = trie_search(nm.file_trie, msg->filename);
    if (!file_meta) {
        response.status = ERR_FILE_NOT_FOUND;
        send_message(client_sock, &response);
        return;
    }
    
    // Check permissions
    if (strcmp(file_meta->owner, msg->sender) != 0 && 
        !check_access(msg->filename, msg->sender, ACCESS_WRITE)) {
        free(file_meta);
        response.status = ERR_ACCESS_DENIED;
        send_message(client_sock, &response);
        return;
    }
    
    // Check if target folder exists (unless moving to root)
    if (strlen(msg->target_path) > 0 && strcmp(msg->target_path, "/") != 0) {
        FolderMetadata *folder_meta = folder_trie_search(nm.folder_trie, msg->target_path);
        if (!folder_meta) {
            free(file_meta);
            response.status = ERR_FILE_NOT_FOUND;
            send_message(client_sock, &response);
            return;
        }
        free(folder_meta);
    }
    
    int ss_id = file_meta->ss_id;
    
    // Forward to SS
    pthread_mutex_lock(&nm.ss_mutex);
    int ss_idx = -1;
    for (int i = 0; i < nm.ss_count; i++) {
        if (nm.ss_list[i].id == ss_id && nm.ss_list[i].active) {
            ss_idx = i;
            break;
        }
    }
    pthread_mutex_unlock(&nm.ss_mutex);
    
    if (ss_idx < 0) {
        free(file_meta);
        response.status = ERR_SS_UNAVAILABLE;
        send_message(client_sock, &response);
        return;
    }
    
    Message ss_msg;
    init_message(&ss_msg);
    ss_msg.type = MSG_MOVE;
    strcpy(ss_msg.filename, msg->filename);
    strcpy(ss_msg.target_path, msg->target_path);
    strcpy(ss_msg.data, file_meta->folder_path);  // Old path (may be empty for root)
    
    pthread_mutex_lock(&nm.ss_sock_mutexes[ss_idx]);
    send_message(nm.ss_list[ss_idx].sock, &ss_msg);
    
    Message ss_response;
    recv_message(nm.ss_list[ss_idx].sock, &ss_response);
    pthread_mutex_unlock(&nm.ss_sock_mutexes[ss_idx]);
    
    if (ss_response.status == SUCCESS) {
        // Update file metadata with new path
        strcpy(file_meta->folder_path, msg->target_path);
        trie_update(nm.file_trie, msg->filename, file_meta);
        cache_put(nm.cache, msg->filename, file_meta);
        response.status = SUCCESS;
        log_formatted(LOG_INFO, "Moved file %s from '%s' to '%s'", 
                     msg->filename, 
                     strlen(ss_msg.data) > 0 ? ss_msg.data : "(root)",
                     strlen(msg->target_path) > 0 ? msg->target_path : "(root)");
    } else {
        response.status = ss_response.status;
    }
    
    free(file_meta);
    send_message(client_sock, &response);
}

void handle_viewfolder(int client_sock, Message *msg) {
    Message response;
    init_message(&response);
    response.type = MSG_DATA;
    
    // Check if we're viewing root (empty path) or a specific folder
    int viewing_root = (strlen(msg->target_path) == 0 || strcmp(msg->target_path, "/") == 0);
    printf("ROOT? : %d\n", viewing_root);

    if (!viewing_root) {
        // Check if folder exists
        printf("Fetching folder data.....\n");
        FolderMetadata *folder_meta = folder_trie_search(nm.folder_trie, msg->target_path);
        if (!folder_meta) {
            response.status = ERR_FILE_NOT_FOUND;
            send_message(client_sock, &response);
            free(folder_meta);
            return;
        }
    }
    
    // Get all files
    printf("Fetching all files....\n");
    FileMetadata *files[MAX_FILES];
    int file_count = trie_get_all_files(nm.file_trie, files, MAX_FILES);
    printf("Found %d files!\n", file_count);
    
    char buffer[MAX_BUFFER] = "";
    int pos = 0;
    
    // Filter files in this folder
    for (int i = 0; i < file_count; i++) {
        int matches = 0;
        
        if (viewing_root) {
            // Show files with empty folder_path
            matches = (strlen(files[i]->folder_path) == 0);
        } else {
            // Show files in specified folder
            printf("Found match!\n");
            matches = (strcmp(files[i]->folder_path, msg->target_path) == 0);
        }
        
        if (matches && check_access(files[i]->filename, msg->sender, ACCESS_READ)) {
            pos += snprintf(buffer + pos, MAX_BUFFER - pos, "%s\n", files[i]->filename);
        }
        free(files[i]);
    }
    
    if (pos == 0) {
        strcpy(buffer, "(empty folder)\n");
    }
    
    strncpy(response.data, buffer, MAX_BUFFER - 1);
    response.status = SUCCESS;
    printf("Setting status to success...\n");
    printf("Created response: %s\n", response.data);
    send_message(client_sock, &response);
}

void handle_checkpoint_request(int client_sock, Message *msg) {
    Message response;
    init_message(&response);
    
    // Check write access
    if (!check_access(msg->filename, msg->sender, ACCESS_WRITE)) {
        response.status = ERR_ACCESS_DENIED;
        send_message(client_sock, &response);
        return;
    }
    
    int ss_id = find_ss_for_file(msg->filename);
    if (ss_id < 0) {
        response.status = ERR_FILE_NOT_FOUND;
        send_message(client_sock, &response);
        return;
    }
    
    pthread_mutex_lock(&nm.ss_mutex);
    int ss_idx = -1;
    for (int i = 0; i < nm.ss_count; i++) {
        if (nm.ss_list[i].id == ss_id && nm.ss_list[i].active) {
            ss_idx = i;
            break;
        }
    }
    pthread_mutex_unlock(&nm.ss_mutex);
    
    if (ss_idx < 0) {
        response.status = ERR_SS_UNAVAILABLE;
        send_message(client_sock, &response);
        return;
    }
    
    // Forward to SS
    pthread_mutex_lock(&nm.ss_sock_mutexes[ss_idx]);
    send_message(nm.ss_list[ss_idx].sock, msg);
    recv_message(nm.ss_list[ss_idx].sock, &response);
    pthread_mutex_unlock(&nm.ss_sock_mutexes[ss_idx]);
    
    send_message(client_sock, &response);
    
    log_formatted(LOG_INFO, "Checkpoint operation type=%d for %s by %s", 
                 msg->type, msg->filename, msg->sender);
}

void handle_view(int client_sock, Message *msg) {
    int show_all = 0;
    int show_details = 0;

    /* Parse flags from msg->data: accept combined/repeated flags like -al, -laaa, -a -l, -all */

    if (msg && msg->data && msg->data[0] != '\0') {
        const char *s = msg->data;
        while (*s) {
            /* skip whitespace */
            while (*s && isspace((unsigned char)*s)) s++;
            if (*s == '\0') break;

            if (*s == '-') {
                /* parse flag token characters until whitespace */
                s++;
                while (*s && !isspace((unsigned char)*s)) {
                    if (*s == 'a') show_all = 1;
                    else if (*s == 'l') show_details = 1;
                    /* ignore unknown flag chars */
                    s++;
                }
            } else {
                /* skip non-flag token */
                while (*s && !isspace((unsigned char)*s)) s++;
            }
        }
    }
    
    FileMetadata *files[MAX_FILES];
    int file_count = trie_get_all_files(nm.file_trie, files, MAX_FILES);
    
    // Fetch metadata for files if needed - N
    if (show_details) {
        for (int i = 0; i < file_count; i++) {
            int ss_id = files[i]->ss_id;
            
            pthread_mutex_lock(&nm.ss_mutex);
            int ss_idx = -1;
            for (int j = 0; j < nm.ss_count; j++) {
                if (nm.ss_list[j].id == ss_id && nm.ss_list[j].active) {
                    ss_idx = j;
                    break;
                }
            }
            pthread_mutex_unlock(&nm.ss_mutex);
            
            if (ss_idx >= 0) {
                Message ss_req;
                init_message(&ss_req);
                ss_req.type = MSG_SS_INFO;
                strcpy(ss_req.filename, files[i]->filename);
                
                pthread_mutex_lock(&nm.ss_sock_mutexes[ss_idx]);
                send_message(nm.ss_list[ss_idx].sock, &ss_req);
                
                Message ss_resp;
                if (recv_message(nm.ss_list[ss_idx].sock, &ss_resp) == 0 && 
                    ss_resp.status == SUCCESS) {
                    printf("Reached here! with %s\n", ss_resp.data);
                    // Changed delimiting to ; to avoid conflict - N
                    // Parse and update metadata
                    sscanf(ss_resp.data, "%zu|%d|%d|%ld|%ld",
                        &files[i]->size, &files[i]->word_count, &files[i]->char_count,
                        &files[i]->modified, &files[i]->accessed);
                    
                    // Update in trie and cache
                    trie_update(nm.file_trie, files[i]->filename, files[i]);
                    cache_put(nm.cache, files[i]->filename, files[i]);
                }
                pthread_mutex_unlock(&nm.ss_sock_mutexes[ss_idx]);
            }
        }
    }
    
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
        int ss_idx = -1;
        for (int i = 0; i < nm.ss_count; i++) {
            if (nm.ss_list[i].id == ss_id) {
                ss_idx = i;
                break;
            }
        }
        pthread_mutex_unlock(&nm.ss_mutex); // Unlock early and use specific SS mutex instead - N
        
        if (ss_idx >= 0) {
            Message ss_req;
            init_message(&ss_req);
            ss_req.type = MSG_SS_INFO;
            strcpy(ss_req.filename, msg->filename);
            
            // FIXED: Lock the specific SS socket - N
            pthread_mutex_lock(&nm.ss_sock_mutexes[ss_idx]);
            send_message(nm.ss_list[ss_idx].sock, &ss_req);
            
            Message ss_resp;
            if (recv_message(nm.ss_list[ss_idx].sock, &ss_resp) == 0 && 
                ss_resp.status == SUCCESS) {
                sscanf(ss_resp.data, "%zu|%d|%d|%ld|%ld",
                       &meta->size, &meta->word_count, &meta->char_count,
                       &meta->modified, &meta->accessed);
            }
            pthread_mutex_unlock(&nm.ss_sock_mutexes[ss_idx]);
        }
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
    pthread_mutex_unlock(&nm.ss_mutex); // Unlock early and use specific SS mutex instead - N
    
    if (ss_idx < 0) {
        response.status = ERR_SS_UNAVAILABLE;
        send_message(client_sock, &response);
        return;
    }
    
    Message ss_msg;
    init_message(&ss_msg);
    ss_msg.type = MSG_CREATE;
    strcpy(ss_msg.filename, msg->filename);
    
    // Lock the specific SS socket - N
    pthread_mutex_lock(&nm.ss_sock_mutexes[ss_idx]);
    send_message(nm.ss_list[ss_idx].sock, &ss_msg);
    
    Message ss_response;
    recv_message(nm.ss_list[ss_idx].sock, &ss_response);
    pthread_mutex_unlock(&nm.ss_sock_mutexes[ss_idx]);
    
    if (ss_response.status == SUCCESS) {
        // Add to trie
        FileMetadata meta;
        memset(&meta, 0, sizeof(FileMetadata));
        strcpy(meta.filename, msg->filename);
        meta.folder_path[0] = '\0';
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
    pthread_mutex_unlock(&nm.ss_mutex); // Unlock early and use specific SS mutex instead - N
    
    if (ss_idx < 0) {
        response.status = ERR_SS_UNAVAILABLE;
        send_message(client_sock, &response);
        return;
    }
    
    Message ss_msg;
    init_message(&ss_msg);
    ss_msg.type = MSG_DELETE;
    strcpy(ss_msg.filename, msg->filename);
    
    // FIXED: Lock the specific SS socket - N
    pthread_mutex_lock(&nm.ss_sock_mutexes[ss_idx]);
    send_message(nm.ss_list[ss_idx].sock, &ss_msg);
    
    Message ss_response;
    recv_message(nm.ss_list[ss_idx].sock, &ss_response);
    pthread_mutex_unlock(&nm.ss_sock_mutexes[ss_idx]);
    
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
    pthread_mutex_unlock(&nm.ss_mutex); // Unlock early and use specific SS mutex instead - N
    
    if (ss_idx < 0) {
        response.status = ERR_SS_UNAVAILABLE;
        send_message(client_sock, &response);
        return;
    }
    
    Message ss_msg;
    init_message(&ss_msg);
    ss_msg.type = MSG_SS_INFO;
    strcpy(ss_msg.filename, msg->filename);
    strcpy(ss_msg.data, "READ_CONTENT");
    
    // FIXED: Lock the specific SS socket - N
    pthread_mutex_lock(&nm.ss_sock_mutexes[ss_idx]);
    send_message(nm.ss_list[ss_idx].sock, &ss_msg);
    
    Message ss_response;
    recv_message(nm.ss_list[ss_idx].sock, &ss_response);
    pthread_mutex_unlock(&nm.ss_sock_mutexes[ss_idx]);

    printf("SS Response Dtaa: %s\n", ss_response.data); // Debug line
    
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

// handles heartbeats explcitly - N
void* ss_hb_listener(void* arg) {
    (void) arg;

    nm.ss_hb_sock = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(nm.ss_hb_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(NM_SS_HB_PORT);
    
    bind(nm.ss_hb_sock, (struct sockaddr*)&addr, sizeof(addr));
    listen(nm.ss_hb_sock, MAX_SS);
    
    printf("[NM] Listening for SS heartbeats on port %d\n", NM_SS_HB_PORT);
    
    while (nm.running) {
        int *hb_sock = malloc(sizeof(int));
        *hb_sock = accept(nm.ss_hb_sock, NULL, NULL);
        
        if (*hb_sock < 0) {
            free(hb_sock);
            continue;
        }
        
        pthread_t tid;
        pthread_create(&tid, NULL, handle_ss_heartbeat, hb_sock);
        pthread_detach(tid);
    }
    
    return NULL;
}

// thread attribute function: heartbeat handler - N
void* handle_ss_heartbeat(void* arg) {
    int hb_sock = *((int*)arg);
    free(arg);
    
    Message msg;
    
    // First message identifies the SS - N
    if (recv_message(hb_sock, &msg) < 0 || msg.type != MSG_ACK) {
        close(hb_sock);
        return NULL;
    }
    
    int my_ss_id = msg.ss_id;
    log_formatted(LOG_INFO, "SS %d heartbeat connection established", my_ss_id);

    pthread_mutex_lock(&nm.ss_mutex);
    for (int i = 0; i < nm.ss_count; i++) {
        if (nm.ss_list[i].id == my_ss_id) {
            nm.ss_list[i].hb_sock = hb_sock;
            nm.ss_list[i].last_heartbeat = time(NULL);
            break;
        }
    }
    pthread_mutex_unlock(&nm.ss_mutex);
    
    // Receive heartbeats - N
    while (nm.running) {
        if (recv_message(hb_sock, &msg) < 0) {
            log_formatted(LOG_WARNING, "SS %d heartbeat connection lost", my_ss_id);
            break;
        }
        
        if (msg.type == MSG_ACK && strcmp(msg.data, "HEARTBEAT") == 0) {
            pthread_mutex_lock(&nm.ss_mutex);
            for (int i = 0; i < nm.ss_count; i++) {
                if (nm.ss_list[i].id == my_ss_id) {
                    nm.ss_list[i].last_heartbeat = time(NULL);
                    log_formatted(LOG_DEBUG, "Heartbeat from SS %d", my_ss_id);
                    break;
                }
            }
            pthread_mutex_unlock(&nm.ss_mutex);
        }
    }

    // Heartbeat lost - mark as inactive - N
    pthread_mutex_lock(&nm.ss_mutex);
    for (int i = 0; i < nm.ss_count; i++) {
        if (nm.ss_list[i].id == my_ss_id) {
            nm.ss_list[i].hb_sock = -1;
            nm.ss_list[i].active = 0;  // THIS triggers cleanup in handle_ss_connection
            log_formatted(LOG_ERROR, "SS %d marked INACTIVE due to heartbeat failure", my_ss_id);
            break;
        }
    }
    pthread_mutex_unlock(&nm.ss_mutex);
    
    close(hb_sock);
    return NULL;
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
    
    // Check if this SS ID already exists (reconnection scenario) - N
    int existing_idx = -1;
    for (int i = 0; i < nm.ss_count; i++) {
        if (nm.ss_list[i].id == msg.ss_id) {
            existing_idx = i;
            log_formatted(LOG_INFO, "SS %d reconnecting - replacing old connection", msg.ss_id);
            break;
        }
    }
    
    int idx;
    if (existing_idx >= 0) {
        // Reconnection: close old sockets and reuse the slot -  N
        idx = existing_idx;
        
        // Mark old connection as dead immediately - N
        nm.ss_list[idx].active = 0;
        
        // Close old sockets - N
        if (nm.ss_list[idx].sock >= 0) {
            close(nm.ss_list[idx].sock);
        }
        if (nm.ss_list[idx].hb_sock >= 0) {
            close(nm.ss_list[idx].hb_sock);
        }
        
        log_formatted(LOG_INFO, "Closed old sockets for SS %d", msg.ss_id);
    } else {
        // New SS: check capacity - N
        if (nm.ss_count >= MAX_SS) {
            pthread_mutex_unlock(&nm.ss_mutex);
            close(ss_sock);
            log_formatted(LOG_ERROR, "Cannot accept SS %d: max capacity reached", msg.ss_id);
            return NULL;
        }
        idx = nm.ss_count;
        nm.ss_count++;
    }
    
    nm.ss_list[idx].id = msg.ss_id;
    strcpy(nm.ss_list[idx].ip, msg.sender);
    nm.ss_list[idx].nm_port = msg.nm_port;
    nm.ss_list[idx].client_port = msg.client_port;  // Use proper field - N
    printf("[NM] Registered SS ID: %d, IP: %s, NM Port: %d, Client Port: %d\n", 
           msg.ss_id, msg.sender, msg.nm_port, msg.client_port);
    nm.ss_list[idx].sock = ss_sock;
    nm.ss_list[idx].hb_sock = -1;  // Initialize, will be set later - N
    nm.ss_list[idx].active = 1;
    // nm.ss_list[idx].last_heartbeat = time(NULL);
    nm.ss_list[idx].file_count = 0;
    
    // Parse file list
    char *file_list = strdup(msg.data);
    char *token = strtok(file_list, ",");
    while (token && nm.ss_list[idx].file_count < MAX_FILES) {
        strcpy(nm.ss_list[idx].files[nm.ss_list[idx].file_count], token);
        
        // Check if file already exists in trie - N
        FileMetadata *existing = trie_search(nm.file_trie, token);
    
        // This means we have a reconnecting SS, so preserve metadata - N
        if (existing) {
            log_formatted(LOG_INFO, "Preserving metadata for existing file: %s (owner: %s)", 
                        token, existing->owner);
        
            existing->ss_id = msg.ss_id;
            trie_update(nm.file_trie, token, existing);
            free(existing);
        } else {
            // The usual, create new - N
            FileMetadata meta;
            memset(&meta, 0, sizeof(FileMetadata));
            strcpy(meta.filename, token);
            meta.ss_id = msg.ss_id;
            strcpy(meta.owner, "system");
            meta.created = time(NULL);
            meta.modified = meta.created;
            meta.accessed = meta.created;
            meta.acl_count = 0;
            
            trie_insert(nm.file_trie, token, &meta);
            log_formatted(LOG_INFO, "Registered new file: %s", token);
        }
        
        nm.ss_list[idx].file_count++;
        token = strtok(NULL, ",");
    }
    free(file_list);

    nm.ss_list[idx].last_heartbeat = time(NULL); // Moved here to give more time for the heartbeat and initialization - N
    
    int my_ss_id = msg.ss_id;
    // nm.ss_count++;
    pthread_mutex_unlock(&nm.ss_mutex);
    
    log_formatted(LOG_INFO, "SS %d registered with %d files", 
                 msg.ss_id, nm.ss_list[idx].file_count);
    printf("[NM] Storage Server %d connected from %s\n", msg.ss_id, msg.sender);
    
    while (nm.running) {
        pthread_mutex_lock(&nm.ss_mutex);
        int still_active = 0;
        for (int i = 0; i < nm.ss_count; i++) {
            if (nm.ss_list[i].id == my_ss_id) {
                still_active = nm.ss_list[i].active;
                break;
            }
        }
        pthread_mutex_unlock(&nm.ss_mutex);
            
        if (!still_active) {
            log_formatted(LOG_INFO, "SS %d marked inactive by heartbeat monitor", my_ss_id);
            break;
        }
            
        sleep(3); // I set it to 3, we can decide on a suitable value later - N
    }
        
    // Cleanup when heartbeat declares it dead
    close(ss_sock);
    log_formatted(LOG_INFO, "Closed command socket for SS %d", my_ss_id);
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
            case MSG_CHECKPOINT:
            case MSG_VIEWCHECKPOINT:
            case MSG_REVERT:
            case MSG_LISTCHECKPOINTS:
                handle_checkpoint_request(client_sock, &msg);
                break;
            case MSG_CREATEFOLDER:
                handle_createfolder(client_sock, &msg);
                break;
            case MSG_MOVE:
                handle_move(client_sock, &msg);
                break;
            case MSG_VIEWFOLDER:
                handle_viewfolder(client_sock, &msg);
                break;
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
                        printf("SS Info sent to client: %s\n", response.data); // Debug line
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
    
   // printf("Client listener binding to port %d\n", NM_CLIENT_PORT);
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
            if (nm.ss_list[i].active) {
                time_t idle = now - nm.ss_list[i].last_heartbeat;
                
                // Give extra grace period (60 seconds) for initial connection - N
                // The grace value can be adjusted as needed - N
                int timeout = (idle < 60) ? 60 : HEARTBEAT_TIMEOUT;
                
                if (idle > timeout) {
                    log_formatted(LOG_WARNING, "SS %d heartbeat timeout (last: %ld sec ago)", 
                                 nm.ss_list[i].id, idle);
                    nm.ss_list[i].active = 0;
                    
                    if (nm.ss_list[i].hb_sock >= 0) {
                        close(nm.ss_list[i].hb_sock);
                        nm.ss_list[i].hb_sock = -1;
                    }
                }
            }
        }
        
        pthread_mutex_unlock(&nm.ss_mutex);
    }
    
    return NULL;
}

int main() {
    init_name_server();
    
    pthread_t ss_thread, ss_hb_thread, client_thread, hb_thread;  // ss_hb_thread - N
    pthread_create(&ss_thread, NULL, ss_listener, NULL);
    pthread_create(&ss_hb_thread, NULL, ss_hb_listener, NULL);     // create and join the threads as necessary - N
    pthread_create(&client_thread, NULL, client_listener, NULL);
    pthread_create(&hb_thread, NULL, heartbeat_monitor, NULL);
    
    printf("[NM] Name Server running. Press Ctrl+C to stop.\n");
    
    pthread_join(ss_thread, NULL);
    pthread_join(ss_hb_thread, NULL);  // join the heartbeat listener thread - N
    pthread_join(client_thread, NULL);
    pthread_join(hb_thread, NULL);
    
    free_trie(nm.file_trie);
    free_cache(nm.cache);
    close_logger();
    
    return 0;
}
