#include "logger.h"
#include <stdarg.h>

static FILE *log_file = NULL;
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

const char* log_level_str(LogLevel level) {
    switch(level) {
        case LOG_INFO: return "INFO";
        case LOG_WARNING: return "WARN";
        case LOG_ERROR: return "ERROR";
        case LOG_DEBUG: return "DEBUG";
        case LOG_REQUEST: return "REQ";
        case LOG_RESPONSE: return "RESP";
        default: return "UNKNOWN";
    }
}

int init_logger(const char *log_filename) {
    pthread_mutex_lock(&log_mutex);
    
    log_file = fopen(log_filename, "a");
    if (!log_file) {
        pthread_mutex_unlock(&log_mutex);
        return -1;
    }
    
    fprintf(log_file, "\n=== Log Started at %s ===\n", get_timestamp());
    fflush(log_file);
    
    pthread_mutex_unlock(&log_mutex);
    return 0;
}

void close_logger() {
    pthread_mutex_lock(&log_mutex);
    
    if (log_file) {
        fprintf(log_file, "=== Log Closed at %s ===\n\n", get_timestamp());
        fclose(log_file);
        log_file = NULL;
    }
    
    pthread_mutex_unlock(&log_mutex);
}

void log_message(LogLevel level, const char *ip, int port, 
                 const char *username, const char *operation, 
                 const char *status, const char *details) {
    pthread_mutex_lock(&log_mutex);
    
    if (log_file) {
        fprintf(log_file, "[%s] [%s] [%s:%d] [User: %s] [Op: %s] [Status: %s] %s\n",
                get_timestamp(),
                log_level_str(level),
                ip ? ip : "N/A",
                port,
                username ? username : "N/A",
                operation ? operation : "N/A",
                status ? status : "N/A",
                details ? details : "");
        fflush(log_file);
    }
    
    pthread_mutex_unlock(&log_mutex);
}

void log_formatted(LogLevel level, const char *format, ...) {
    pthread_mutex_lock(&log_mutex);
    
    if (log_file) {
        fprintf(log_file, "[%s] [%s] ", get_timestamp(), log_level_str(level));
        
        va_list args;
        va_start(args, format);
        vfprintf(log_file, format, args);
        va_end(args);
        
        fprintf(log_file, "\n");
        fflush(log_file);
    }
    
    pthread_mutex_unlock(&log_mutex);
}

void display_and_log(const char *message) {
    printf("%s\n", message);
    log_formatted(LOG_INFO, "%s", message);
}
