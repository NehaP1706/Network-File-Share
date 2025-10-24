#include "common.h"

void init_message(Message *msg) {
    memset(msg, 0, sizeof(Message));
    msg->status = SUCCESS;
    msg->type = MSG_ACK;
    msg->sentence_index = -1;
    msg->word_index = -1;
    msg->ss_id = -1;
    msg->access = ACCESS_NONE;
}

void serialize_message(Message *msg, char *buffer) {
    sprintf(buffer, "%d|%d|%s|%s|%d|%d|%d|%d|%s|%s",
            msg->type,
            msg->status,
            msg->sender,
            msg->filename,
            msg->sentence_index,
            msg->word_index,
            msg->ss_id,
            msg->access,
            msg->target_user,
            msg->data);
}

void deserialize_message(char *buffer, Message *msg) {
    init_message(msg);
    char *p = buffer;
    int field = 0;

    while (field < 10 && p) {
        char *sep = strchr(p, '|');
        size_t len = sep ? (size_t)(sep - p) : strlen(p);

        /* create a temporary null-terminated token (may be empty) */
        char token_buf[MAX_BUFFER];
        if (len > sizeof(token_buf) - 1) len = sizeof(token_buf) - 1;
        memcpy(token_buf, p, len);
        token_buf[len] = '\0';

        if (len > 0) {
            switch (field) {
                case 0: msg->type = atoi(token_buf); break;
                case 1: msg->status = atoi(token_buf); break;
                case 2: strncpy(msg->sender, token_buf, MAX_USERNAME-1); msg->sender[MAX_USERNAME-1] = '\0'; break;
                case 3: strncpy(msg->filename, token_buf, MAX_FILENAME-1); msg->filename[MAX_FILENAME-1] = '\0'; break;
                case 4: msg->sentence_index = atoi(token_buf); break;
                case 5: msg->word_index = atoi(token_buf); break;
                case 6: msg->ss_id = atoi(token_buf); break;
                case 7: msg->access = atoi(token_buf); break;
                case 8: strncpy(msg->target_user, token_buf, MAX_USERNAME-1); msg->target_user[MAX_USERNAME-1] = '\0'; break;
                case 9: strncpy(msg->data, token_buf, MAX_BUFFER-1); msg->data[MAX_BUFFER-1] = '\0'; break;
            }
        } else {
            /* empty token: keep defaults from init_message() for numeric fields,
               and leave strings as empty (already zeroed by init_message). */
            if (field == 2) msg->sender[0] = '\0';
            if (field == 3) msg->filename[0] = '\0';
            if (field == 8) msg->target_user[0] = '\0';
            if (field == 9) msg->data[0] = '\0';
        }

        field++;
        if (!sep) break;
        p = sep + 1;
    }
    // implemented a parser that can handle empty fields correctly instead of strtok and strtok_r - S
}

int send_message(int sock, Message *msg) {
    char buffer[MAX_BUFFER * 2];
    serialize_message(msg, buffer);
    
    int len = strlen(buffer);
    int total_sent = 0;
    
    // Send length first
    if (send(sock, &len, sizeof(int), 0) < 0) {
        return -1;
    }
    
    // Send data
    while (total_sent < len) {
        int sent = send(sock, buffer + total_sent, len - total_sent, 0);
        if (sent < 0) {
            return -1;
        }
        total_sent += sent;
    }
    
    return 0;
}

int recv_message(int sock, Message *msg) {
    int len;
    
    // Receive length first
    int received = recv(sock, &len, sizeof(int), MSG_WAITALL);
    if (received <= 0) {
        return -1;
    }
    
    if (len >= MAX_BUFFER * 2 || len <= 0) {
        return -1;
    }
    
    // Receive data
    char buffer[MAX_BUFFER * 2];
    int total_received = 0;
    
    while (total_received < len) {
        received = recv(sock, buffer + total_received, len - total_received, 0);
        if (received <= 0) {
            return -1;
        }
        total_received += received;
    }
    
    buffer[len] = '\0';
    deserialize_message(buffer, msg);
    
    return 0;
}

char* get_timestamp() {
    static char timestamp[64];
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now); //changed localtime_r to localtime - S
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);
    return timestamp;
}

void trim_whitespace(char *str) {
    char *end;
    
    // Trim leading space
    while (*str == ' ' || *str == '\t' || *str == '\n' || *str == '\r') {
        str++;
    }
    
    if (*str == 0) return;
    
    // Trim trailing space
    end = str + strlen(str) - 1;
    while (end > str && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')) {
        end--;
    }
    
    *(end + 1) = '\0';
}
