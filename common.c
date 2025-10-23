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
    char *token;
    char *rest = buffer;
    int field = 0;
    
    while ((token = strtok_r(rest, "|", &rest)) && field < 10) {
        switch(field) {
            case 0: msg->type = atoi(token); break;
            case 1: msg->status = atoi(token); break;
            case 2: strncpy(msg->sender, token, MAX_USERNAME-1); break;
            case 3: strncpy(msg->filename, token, MAX_FILENAME-1); break;
            case 4: msg->sentence_index = atoi(token); break;
            case 5: msg->word_index = atoi(token); break;
            case 6: msg->ss_id = atoi(token); break;
            case 7: msg->access = atoi(token); break;
            case 8: strncpy(msg->target_user, token, MAX_USERNAME-1); break;
            case 9: strncpy(msg->data, token, MAX_BUFFER-1); break;
        }
        field++;
    }
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
