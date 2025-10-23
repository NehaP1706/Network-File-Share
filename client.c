#include "common.h"

#define NM_IP "127.0.0.1"
#define NM_PORT 8081

typedef struct {
    char username[MAX_USERNAME];
    int nm_sock;
    int connected;
} Client;

Client client;

// Function declarations
void init_client();
void connect_to_nm();
void command_loop();
void handle_view(char *args);
void handle_read(char *filename);
void handle_create(char *filename);
void handle_write(char *filename, char *sent_idx_str);
void handle_delete(char *filename);
void handle_info(char *filename);
void handle_stream(char *filename);
void handle_list();
void handle_addaccess(char *flag, char *filename, char *username);
void handle_remaccess(char *filename, char *username);
void handle_exec(char *filename);
void handle_undo(char *filename);
int connect_to_ss(const char *ss_info);
void print_error(int status);

void init_client() {
    printf("Enter username: ");
    fgets(client.username, MAX_USERNAME, stdin);
    trim_whitespace(client.username);
    
    client.connected = 0;
    
    printf("[Client] Username: %s\n", client.username);
}

void connect_to_nm() {
    client.nm_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (client.nm_sock < 0) {
        perror("Socket creation failed");
        exit(1);
    }
    
    struct sockaddr_in nm_addr;
    nm_addr.sin_family = AF_INET;
    nm_addr.sin_port = htons(NM_PORT);
    inet_pton(AF_INET, NM_IP, &nm_addr.sin_addr);
    
    if (connect(client.nm_sock, (struct sockaddr*)&nm_addr, sizeof(nm_addr)) < 0) {
        perror("Connection to NM failed");
        exit(1);
    }
    
    // Send registration
    Message msg;
    init_message(&msg);
    msg.type = MSG_REG_CLIENT;
    strcpy(msg.sender, client.username);
    strcpy(msg.data, "127.0.0.1");
    
    send_message(client.nm_sock, &msg);
    
    Message response;
    recv_message(client.nm_sock, &response);
    
    if (response.status == SUCCESS) {
        client.connected = 1;
        printf("[Client] Connected to Name Server\n");
    } else {
        printf("[Client] Registration failed\n");
        exit(1);
    }
}

void print_error(int status) {
    switch (status) {
        case ERR_FILE_NOT_FOUND:
            printf("Error: File not found\n");
            break;
        case ERR_ACCESS_DENIED:
            printf("Error: Access denied\n");
            break;
        case ERR_SENTENCE_LOCKED:
            printf("Error: Sentence is locked by another user\n");
            break;
        case ERR_INVALID_INDEX:
            printf("Error: Invalid sentence or word index\n");
            break;
        case ERR_FILE_EXISTS:
            printf("Error: File already exists\n");
            break;
        case ERR_SS_UNAVAILABLE:
            printf("Error: Storage server unavailable\n");
            break;
        case ERR_INVALID_OPERATION:
            printf("Error: Invalid operation\n");
            break;
        case ERR_NOT_OWNER:
            printf("Error: You are not the owner of this file\n");
            break;
        case ERR_USER_NOT_FOUND:
            printf("Error: User not found\n");
            break;
        default:
            printf("Error: Unknown error (code %d)\n", status);
            break;
    }
}

int connect_to_ss(const char *ss_info) {
    char ip[INET_ADDRSTRLEN];
    int port;
    
    sscanf(ss_info, "%[^:]:%d", ip, &port);
    
    int ss_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (ss_sock < 0) {
        return -1;
    }
    
    struct sockaddr_in ss_addr;
    ss_addr.sin_family = AF_INET;
    ss_addr.sin_port = htons(port);
    inet_pton(AF_INET, ip, &ss_addr.sin_addr);
    
    if (connect(ss_sock, (struct sockaddr*)&ss_addr, sizeof(ss_addr)) < 0) {
        close(ss_sock);
        return -1;
    }
    
    return ss_sock;
}

void handle_view(char *args) {
    Message msg;
    init_message(&msg);
    msg.type = MSG_VIEW;
    strcpy(msg.sender, client.username);
    
    if (args) {
        strncpy(msg.data, args, MAX_BUFFER - 1);
    }
    
    send_message(client.nm_sock, &msg);
    
    Message response;
    recv_message(client.nm_sock, &response);
    
    if (response.status == SUCCESS) {
        printf("%s", response.data);
    } else {
        print_error(response.status);
    }
}

void handle_read(char *filename) {
    Message msg;
    init_message(&msg);
    msg.type = MSG_READ;
    strcpy(msg.sender, client.username);
    strcpy(msg.filename, filename);
    
    send_message(client.nm_sock, &msg);
    
    Message response;
    recv_message(client.nm_sock, &response);
    
    if (response.status != SUCCESS) {
        print_error(response.status);
        return;
    }
    
    // Connect to SS
    int ss_sock = connect_to_ss(response.data);
    if (ss_sock < 0) {
        printf("Error: Could not connect to storage server\n");
        return;
    }
    
    // Send read request to SS
    init_message(&msg);
    msg.type = MSG_READ;
    strcpy(msg.filename, filename);
    strcpy(msg.sender, client.username);
    
    send_message(ss_sock, &msg);
    recv_message(ss_sock, &response);
    
    if (response.status == SUCCESS) {
        printf("%s\n", response.data);
    } else {
        print_error(response.status);
    }
    
    close(ss_sock);
}

void handle_create(char *filename) {
    Message msg;
    init_message(&msg);
    msg.type = MSG_CREATE;
    strcpy(msg.sender, client.username);
    strcpy(msg.filename, filename);
    
    send_message(client.nm_sock, &msg);
    
    Message response;
    recv_message(client.nm_sock, &response);
    
    if (response.status == SUCCESS) {
        printf("File created successfully!\n");
    } else {
        print_error(response.status);
    }
}

void handle_write(char *filename, char *sent_idx_str) {
    int sent_idx = atoi(sent_idx_str);
    
    // Get SS info from NM
    Message msg;
    init_message(&msg);
    msg.type = MSG_WRITE;
    strcpy(msg.sender, client.username);
    strcpy(msg.filename, filename);
    
    send_message(client.nm_sock, &msg);
    
    Message response;
    recv_message(client.nm_sock, &response);
    
    if (response.status != SUCCESS) {
        print_error(response.status);
        return;
    }
    
    // Connect to SS
    int ss_sock = connect_to_ss(response.data);
    if (ss_sock < 0) {
        printf("Error: Could not connect to storage server\n");
        return;
    }
    
    // Lock sentence
    init_message(&msg);
    msg.type = MSG_LOCK_SENTENCE;
    strcpy(msg.filename, filename);
    strcpy(msg.sender, client.username);
    msg.sentence_index = sent_idx;
    
    send_message(ss_sock, &msg);
    recv_message(ss_sock, &response);
    
    if (response.status != SUCCESS) {
        print_error(response.status);
        close(ss_sock);
        return;
    }
    
    printf("Sentence locked. Enter writes (word_index content), then type ETIRW:\n");
    
    // Read write commands
    char line[MAX_BUFFER];
    int write_count = 0;
    
    while (1) {
        printf("Client: ");
        if (!fgets(line, sizeof(line), stdin)) {
            break;
        }
        
        trim_whitespace(line);
        
        if (strcmp(line, "ETIRW") == 0) {
            break;
        }
        
        // Parse word_index and content
        int word_idx;
        char content[MAX_BUFFER];
        
        if (sscanf(line, "%d %[^\n]", &word_idx, content) != 2) {
            printf("Invalid format. Use: <word_index> <content>\n");
            continue;
        }
        
        // Send write to SS
        init_message(&msg);
        msg.type = MSG_WRITE;
        strcpy(msg.filename, filename);
        strcpy(msg.sender, client.username);
        msg.sentence_index = sent_idx;
        msg.word_index = word_idx;
        strcpy(msg.data, content);
        
        send_message(ss_sock, &msg);
        recv_message(ss_sock, &response);
        
        if (response.status != SUCCESS) {
            print_error(response.status);
            break;
        }
        
        write_count++;
    }
    
    // Unlock sentence
    init_message(&msg);
    msg.type = MSG_UNLOCK_SENTENCE;
    strcpy(msg.filename, filename);
    strcpy(msg.sender, client.username);
    msg.sentence_index = sent_idx;
    
    send_message(ss_sock, &msg);
    recv_message(ss_sock, &response);
    
    if (response.status == SUCCESS && write_count > 0) {
        printf("Write successful!\n");
    } else if (write_count == 0) {
        printf("No writes performed.\n");
    }
    
    close(ss_sock);
}

void handle_delete(char *filename) {
    Message msg;
    init_message(&msg);
    msg.type = MSG_DELETE;
    strcpy(msg.sender, client.username);
    strcpy(msg.filename, filename);
    
    send_message(client.nm_sock, &msg);
    
    Message response;
    recv_message(client.nm_sock, &response);
    
    if (response.status == SUCCESS) {
        printf("File deleted successfully!\n");
    } else {
        print_error(response.status);
    }
}

void handle_info(char *filename) {
    Message msg;
    init_message(&msg);
    msg.type = MSG_INFO;
    strcpy(msg.sender, client.username);
    strcpy(msg.filename, filename);
    
    send_message(client.nm_sock, &msg);
    
    Message response;
    recv_message(client.nm_sock, &response);
    
    if (response.status == SUCCESS) {
        printf("%s", response.data);
    } else {
        print_error(response.status);
    }
}

void handle_stream(char *filename) {
    Message msg;
    init_message(&msg);
    msg.type = MSG_STREAM;
    strcpy(msg.sender, client.username);
    strcpy(msg.filename, filename);
    
    send_message(client.nm_sock, &msg);
    
    Message response;
    recv_message(client.nm_sock, &response);
    
    if (response.status != SUCCESS) {
        print_error(response.status);
        return;
    }
    
    // Connect to SS
    int ss_sock = connect_to_ss(response.data);
    if (ss_sock < 0) {
        printf("Error: Could not connect to storage server\n");
        return;
    }
    
    // Send stream request
    init_message(&msg);
    msg.type = MSG_STREAM;
    strcpy(msg.filename, filename);
    strcpy(msg.sender, client.username);
    
    send_message(ss_sock, &msg);
    recv_message(ss_sock, &response);
    
    if (response.status != SUCCESS) {
        print_error(response.status);
        close(ss_sock);
        return;
    }
    
    // Receive and display words
    while (1) {
        if (recv_message(ss_sock, &response) < 0) {
            printf("\nError: Connection to storage server lost\n");
            break;
        }
        
        if (response.type == MSG_STOP) {
            printf("\n");
            break;
        }
        
        printf("%s ", response.data);
        fflush(stdout);
    }
    
    close(ss_sock);
}

void handle_list() {
    Message msg;
    init_message(&msg);
    msg.type = MSG_LIST;
    strcpy(msg.sender, client.username);
    
    send_message(client.nm_sock, &msg);
    
    Message response;
    recv_message(client.nm_sock, &response);
    
    if (response.status == SUCCESS) {
        printf("%s", response.data);
    } else {
        print_error(response.status);
    }
}

void handle_addaccess(char *flag, char *filename, char *username) {
    Message msg;
    init_message(&msg);
    msg.type = MSG_ADDACCESS;
    strcpy(msg.sender, client.username);
    strcpy(msg.filename, filename);
    strcpy(msg.target_user, username);
    
    if (strcmp(flag, "-R") == 0) {
        msg.access = ACCESS_READ;
    } else if (strcmp(flag, "-W") == 0) {
        msg.access = ACCESS_READWRITE;
    } else {
        printf("Invalid flag. Use -R for read or -W for write\n");
        return;
    }
    
    send_message(client.nm_sock, &msg);
    
    Message response;
    recv_message(client.nm_sock, &response);
    
    if (response.status == SUCCESS) {
        printf("Access granted successfully!\n");
    } else {
        print_error(response.status);
    }
}

void handle_remaccess(char *filename, char *username) {
    Message msg;
    init_message(&msg);
    msg.type = MSG_REMACCESS;
    strcpy(msg.sender, client.username);
    strcpy(msg.filename, filename);
    strcpy(msg.target_user, username);
    
    send_message(client.nm_sock, &msg);
    
    Message response;
    recv_message(client.nm_sock, &response);
    
    if (response.status == SUCCESS) {
        printf("Access removed successfully!\n");
    } else {
        print_error(response.status);
    }
}

void handle_exec(char *filename) {
    Message msg;
    init_message(&msg);
    msg.type = MSG_EXEC;
    strcpy(msg.sender, client.username);
    strcpy(msg.filename, filename);
    
    send_message(client.nm_sock, &msg);
    
    Message response;
    recv_message(client.nm_sock, &response);
    
    if (response.status == SUCCESS) {
        printf("%s", response.data);
    } else {
        print_error(response.status);
    }
}

void handle_undo(char *filename) {
    Message msg;
    init_message(&msg);
    msg.type = MSG_UNDO;
    strcpy(msg.sender, client.username);
    strcpy(msg.filename, filename);
    
    send_message(client.nm_sock, &msg);
    
    Message response;
    recv_message(client.nm_sock, &response);
    
    if (response.status != SUCCESS) {
        print_error(response.status);
        return;
    }
    
    // Connect to SS
    int ss_sock = connect_to_ss(response.data);
    if (ss_sock < 0) {
        printf("Error: Could not connect to storage server\n");
        return;
    }
    
    // Send undo request
    init_message(&msg);
    msg.type = MSG_UNDO;
    strcpy(msg.filename, filename);
    strcpy(msg.sender, client.username);
    
    send_message(ss_sock, &msg);
    recv_message(ss_sock, &response);
    
    if (response.status == SUCCESS) {
        printf("Undo successful!\n");
    } else {
        print_error(response.status);
    }
    
    close(ss_sock);
}

void command_loop() {
    char line[MAX_BUFFER];
    
    printf("\nWelcome %s! Type commands (or 'help' for list, 'exit' to quit):\n", client.username);
    
    while (1) {
        printf("\n> ");
        if (!fgets(line, sizeof(line), stdin)) {
            break;
        }
        
        trim_whitespace(line);
        
        if (strlen(line) == 0) {
            continue;
        }
        
        // Parse command
        char cmd[64], arg1[MAX_FILENAME], arg2[MAX_FILENAME], arg3[MAX_USERNAME];
        int argc = sscanf(line, "%s %s %s %s", cmd, arg1, arg2, arg3);
        
        if (argc < 1) {
            continue;
        }
        
        if (strcmp(cmd, "exit") == 0 || strcmp(cmd, "quit") == 0) {
            printf("Goodbye!\n");
            break;
        } else if (strcmp(cmd, "help") == 0) {
            printf("Available commands:\n");
            printf("  VIEW [-a] [-l] [-al]  - List files\n");
            printf("  READ <filename>       - Read file content\n");
            printf("  CREATE <filename>     - Create new file\n");
            printf("  WRITE <filename> <sent_idx> - Write to file\n");
            printf("  DELETE <filename>     - Delete file\n");
            printf("  INFO <filename>       - Get file information\n");
            printf("  STREAM <filename>     - Stream file content\n");
            printf("  LIST                  - List all users\n");
            printf("  ADDACCESS -R|-W <filename> <username> - Add access\n");
            printf("  REMACCESS <filename> <username> - Remove access\n");
            printf("  EXEC <filename>       - Execute file as commands\n");
            printf("  UNDO <filename>       - Undo last change\n");
            printf("  exit                  - Exit client\n");
        } else if (strcmp(cmd, "VIEW") == 0) {
            handle_view(argc > 1 ? arg1 : NULL);
        } else if (strcmp(cmd, "READ") == 0) {
            if (argc < 2) {
                printf("Usage: READ <filename>\n");
            } else {
                handle_read(arg1);
            }
        } else if (strcmp(cmd, "CREATE") == 0) {
            if (argc < 2) {
                printf("Usage: CREATE <filename>\n");
            } else {
                handle_create(arg1);
            }
        } else if (strcmp(cmd, "WRITE") == 0) {
            if (argc < 3) {
                printf("Usage: WRITE <filename> <sentence_index>\n");
            } else {
                handle_write(arg1, arg2);
            }
        } else if (strcmp(cmd, "DELETE") == 0) {
            if (argc < 2) {
                printf("Usage: DELETE <filename>\n");
            } else {
                handle_delete(arg1);
            }
        } else if (strcmp(cmd, "INFO") == 0) {
            if (argc < 2) {
                printf("Usage: INFO <filename>\n");
            } else {
                handle_info(arg1);
            }
        } else if (strcmp(cmd, "STREAM") == 0) {
            if (argc < 2) {
                printf("Usage: STREAM <filename>\n");
            } else {
                handle_stream(arg1);
            }
        } else if (strcmp(cmd, "LIST") == 0) {
            handle_list();
        } else if (strcmp(cmd, "ADDACCESS") == 0) {
            if (argc < 4) {
                printf("Usage: ADDACCESS -R|-W <filename> <username>\n");
            } else {
                handle_addaccess(arg1, arg2, arg3);
            }
        } else if (strcmp(cmd, "REMACCESS") == 0) {
            if (argc < 3) {
                printf("Usage: REMACCESS <filename> <username>\n");
            } else {
                handle_remaccess(arg1, arg2);
            }
        } else if (strcmp(cmd, "EXEC") == 0) {
            if (argc < 2) {
                printf("Usage: EXEC <filename>\n");
            } else {
                handle_exec(arg1);
            }
        } else if (strcmp(cmd, "UNDO") == 0) {
            if (argc < 2) {
                printf("Usage: UNDO <filename>\n");
            } else {
                handle_undo(arg1);
            }
        } else {
            printf("Unknown command: %s\n", cmd);
            printf("Type 'help' for list of commands\n");
        }
    }
}

int main() {
    init_client();
    connect_to_nm();
    command_loop();
    
    close(client.nm_sock);
    return 0;
}
