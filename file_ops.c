#include "file_ops.h"
#include "logger.h"
#include <ctype.h>

FileContent* init_file_content() {
    FileContent *fc = malloc(sizeof(FileContent));
    fc->capacity = 10;
    fc->sentence_count = 0;
    fc->sentences = malloc(sizeof(Sentence) * fc->capacity);
    return fc;
}

void free_file_content(FileContent *fc) {
    if (!fc) return;
    
    for (int i = 0; i < fc->sentence_count; i++) {
        for (int j = 0; j < fc->sentences[i].word_count; j++) {
            free(fc->sentences[i].words[j]);
        }
        free(fc->sentences[i].words);
    }
    free(fc->sentences);
    free(fc);
}

int is_delimiter(char c) {
    return (c == '.' || c == '!' || c == '?');
}

char** split_by_delimiters(const char *word, int *count) {
    int cap = 10;
    char **parts = malloc(sizeof(char*) * cap);
    *count = 0;
    
    char buffer[MAX_WORD];
    int buf_idx = 0;
    
    for (int i = 0; word[i] != '\0'; i++) {
        if (is_delimiter(word[i])) {
            // Save current buffer if not empty
            if (buf_idx > 0) {
                buffer[buf_idx] = '\0';
                parts[*count] = strdup(buffer);
                (*count)++;
                buf_idx = 0;
                
                if (*count >= cap) {
                    cap *= 2;
                    parts = realloc(parts, sizeof(char*) * cap);
                }
            }
            
            // Save delimiter as separate word
            buffer[0] = word[i];
            buffer[1] = '\0';
            parts[*count] = strdup(buffer);
            (*count)++;
            
            if (*count >= cap) {
                cap *= 2;
                parts = realloc(parts, sizeof(char*) * cap);
            }
        } else {
            buffer[buf_idx++] = word[i];
        }
    }
    
    // Save remaining buffer
    if (buf_idx > 0) {
        buffer[buf_idx] = '\0';
        parts[*count] = strdup(buffer);
        (*count)++;
    }
    
    return parts;
}

int parse_file(const char *filepath, FileContent *fc) {
    FILE *file = fopen(filepath, "r");
    if (!file) return -1;
    
    fc->sentence_count = 0;
    
    // char buffer[MAX_BUFFER];
    char word[MAX_WORD];
    int word_idx = 0;
    
    // Initialize first sentence
    if (fc->sentence_count >= fc->capacity) {
        fc->capacity *= 2;
        fc->sentences = realloc(fc->sentences, sizeof(Sentence) * fc->capacity);
    }
    fc->sentences[fc->sentence_count].capacity = 10;
    fc->sentences[fc->sentence_count].word_count = 0;
    fc->sentences[fc->sentence_count].words = malloc(sizeof(char*) * fc->sentences[fc->sentence_count].capacity);
    
    int current_sent = 0;
    int c;
    
    while ((c = fgetc(file)) != EOF) {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            if (word_idx > 0) {
                word[word_idx] = '\0';
                
                // Add word to current sentence
                Sentence *sent = &fc->sentences[current_sent];
                if (sent->word_count >= sent->capacity) {
                    sent->capacity *= 2;
                    sent->words = realloc(sent->words, sizeof(char*) * sent->capacity);
                }
                sent->words[sent->word_count++] = strdup(word);
                
                word_idx = 0;
            }
        } else if (is_delimiter(c)) {
            // Save current word if exists
            if (word_idx > 0) {
                word[word_idx] = '\0';
                Sentence *sent = &fc->sentences[current_sent];
                if (sent->word_count >= sent->capacity) {
                    sent->capacity *= 2;
                    sent->words = realloc(sent->words, sizeof(char*) * sent->capacity);
                }
                sent->words[sent->word_count++] = strdup(word);
                word_idx = 0;
            }
            
            // Add delimiter to current sentence
            word[0] = c;
            word[1] = '\0';
            Sentence *sent = &fc->sentences[current_sent];
            if (sent->word_count >= sent->capacity) {
                sent->capacity *= 2;
                sent->words = realloc(sent->words, sizeof(char*) * sent->capacity);
            }
            sent->words[sent->word_count++] = strdup(word);
            
            // Start new sentence
            current_sent++;
            fc->sentence_count = current_sent + 1;
            
            if (fc->sentence_count >= fc->capacity) {
                fc->capacity *= 2;
                fc->sentences = realloc(fc->sentences, sizeof(Sentence) * fc->capacity);
            }
            fc->sentences[current_sent].capacity = 10;
            fc->sentences[current_sent].word_count = 0;
            fc->sentences[current_sent].words = malloc(sizeof(char*) * fc->sentences[current_sent].capacity);
        } else {
            word[word_idx++] = c;
            if (word_idx >= MAX_WORD - 1) {
                word[word_idx] = '\0';
                Sentence *sent = &fc->sentences[current_sent];
                if (sent->word_count >= sent->capacity) {
                    sent->capacity *= 2;
                    sent->words = realloc(sent->words, sizeof(char*) * sent->capacity);
                }
                sent->words[sent->word_count++] = strdup(word);
                word_idx = 0;
            }
        }
    }
    
    // Handle remaining word
    if (word_idx > 0) {
        word[word_idx] = '\0';
        Sentence *sent = &fc->sentences[current_sent];
        if (sent->word_count >= sent->capacity) {
            sent->capacity *= 2;
            sent->words = realloc(sent->words, sizeof(char*) * sent->capacity);
        }
        sent->words[sent->word_count++] = strdup(word);
    }
    
    // Remove empty last sentence if exists
    if (fc->sentence_count > 0 && fc->sentences[fc->sentence_count - 1].word_count == 0) {
        free(fc->sentences[fc->sentence_count - 1].words);
        fc->sentence_count--;
    }

    
    if(fc->sentences[current_sent].word_count > 0) {
        fc->sentence_count = current_sent + 1;
    } //FOR WHEN FILE HAS NO DELIMITERS BUT WORDS, WE STILL COUNT IT AS ONE SENTENCE -S

    fclose(file);
    return 0;
}

int write_file_content(const char *filepath, FileContent *fc) {
    FILE *file = fopen(filepath, "w");
    if (!file) return -1;
    
    for (int i = 0; i < fc->sentence_count; i++) {
        for (int j = 0; j < fc->sentences[i].word_count; j++) {
            fprintf(file, "%s", fc->sentences[i].words[j]);
            
            // Add space after non-delimiter words (except last word in sentence)
            if (j < fc->sentences[i].word_count - 1 && 
                !is_delimiter(fc->sentences[i].words[j][0])) {
                fprintf(file, " ");
            }
        }
        
        // Add space between sentences (except after last)
        if (i < fc->sentence_count - 1) {
            fprintf(file, " ");
        }
    }
    
    fclose(file);
    return 0;
}

char* file_content_to_string(FileContent *fc) {
    char *result = malloc(MAX_BUFFER);
    result[0] = '\0';
    int pos = 0;
    
    for (int i = 0; i < fc->sentence_count; i++) {
        for (int j = 0; j < fc->sentences[i].word_count; j++) {
            int len = strlen(fc->sentences[i].words[j]);
            if (pos + len + 2 < MAX_BUFFER) {
                strcpy(result + pos, fc->sentences[i].words[j]);
                pos += len;
                
                if (j < fc->sentences[i].word_count - 1 && 
                    !is_delimiter(fc->sentences[i].words[j][0])) {
                    result[pos++] = ' ';
                }
            }
        }
        if (i < fc->sentence_count - 1) {
            result[pos++] = ' ';
        }
    }
    
    result[pos] = '\0';
    return result;
}

// Heavily edited - N
int insert_word_in_sentence(FileContent *fc, int sent_idx, int word_idx, const char *word) {
    // Checking validity, might immediately return - N
    if (sent_idx < 0 || sent_idx >= fc->sentence_count) {
        log_formatted(LOG_ERROR, "Invalid sentence index: %d (file has %d sentences)", 
                     sent_idx, fc->sentence_count);
        return -1;
    }
    
    Sentence *sent = &fc->sentences[sent_idx];
    
    // word_idx is 1-based, validate directly - N
    if (word_idx < 1 || word_idx > sent->word_count + 1) {
        log_formatted(LOG_ERROR, "Invalid word index: %d (sentence has %d words, valid range: 1-%d)", 
                     word_idx, sent->word_count, sent->word_count + 1);
        return -1;
    }
    
    // Convert to 0-based - N
    int actual_idx = word_idx - 1;
    
    // Split word by delimiters - N
    int part_count;
    char **parts = split_by_delimiters(word, &part_count);
    
    if (part_count == 0) {
        free(parts);
        log_formatted(LOG_WARNING, "Empty word, skipping insertion");
        return 0;
    }
    
    // Count how many delimiters (new sentences) we're creating - N
    int new_sentences = 0;
    for (int i = 0; i < part_count; i++) {
        if (is_delimiter(parts[i][0])) {
            new_sentences++;
        }
    }
    
    log_formatted(LOG_DEBUG, "Inserting %d parts, %d will create new sentences", 
                 part_count, new_sentences);
    
    // Make room for new sentences if needed - N
    if (new_sentences > 0) {
        int new_total = fc->sentence_count + new_sentences;
        if (new_total > fc->capacity) {
            while (fc->capacity < new_total) fc->capacity *= 2;
            fc->sentences = realloc(fc->sentences, sizeof(Sentence) * fc->capacity);
        }
        
        // Shift existing sentences down to make room
        for (int i = fc->sentence_count - 1; i > sent_idx; i--) {
            fc->sentences[i + new_sentences] = fc->sentences[i];
        }
        
        // Initialize new sentence slots - N
        for (int i = 1; i <= new_sentences; i++) {
            fc->sentences[sent_idx + i].capacity = 10;
            fc->sentences[sent_idx + i].word_count = 0;
            fc->sentences[sent_idx + i].words = malloc(sizeof(char*) * 10);
        }
    }
    
    // Insert parts
    int current_sent_offset = 0;
    Sentence *cur_sent = &fc->sentences[sent_idx];
    
    for (int i = 0; i < part_count; i++) {
        if (is_delimiter(parts[i][0])) {
            // Add delimiter to current sentence
            if (cur_sent->word_count >= cur_sent->capacity) {
                cur_sent->capacity *= 2;
                cur_sent->words = realloc(cur_sent->words, sizeof(char*) * cur_sent->capacity);
            }
            
            // Shift words in current sentence to make room
            for (int j = cur_sent->word_count; j > actual_idx; j--) {
                cur_sent->words[j] = cur_sent->words[j - 1];
            }
            
            cur_sent->words[actual_idx] = strdup(parts[i]);
            cur_sent->word_count++;
            
            // Move to next sentence
            current_sent_offset++;
            cur_sent = &fc->sentences[sent_idx + current_sent_offset];
            actual_idx = 0;  // Reset index for new sentence
        } else {
            // Expand words array if needed
            if (cur_sent->word_count >= cur_sent->capacity) {
                cur_sent->capacity *= 2;
                cur_sent->words = realloc(cur_sent->words, sizeof(char*) * cur_sent->capacity);
            }
            
            // Shift words to make room
            for (int j = cur_sent->word_count; j > actual_idx; j--) {
                cur_sent->words[j] = cur_sent->words[j - 1];
            }
            
            // Insert word
            cur_sent->words[actual_idx] = strdup(parts[i]);
            cur_sent->word_count++;
            actual_idx++;
        }
        
        free(parts[i]);
    }
    free(parts);
    
    fc->sentence_count += new_sentences;
    log_formatted(LOG_DEBUG, "After insertion, file has %d sentences", fc->sentence_count);
    
    return new_sentences;
}

void get_file_stats(const char *filepath, int *word_count, int *char_count) {
    *word_count = 0;
    *char_count = 0;
    
    FILE *file = fopen(filepath, "r");
    if (!file) return;
    
    int in_word = 0;
    int c;
    
    while ((c = fgetc(file)) != EOF) {
        (*char_count)++;
        
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            in_word = 0;
        } else {
            if (!in_word) {
                (*word_count)++;
                in_word = 1;
            }
        }
    }
    
    fclose(file);
}

int create_undo_backup(const char *filepath) {
    char undo_path[MAX_PATH];
    snprintf(undo_path, sizeof(undo_path), "%s.undo", filepath);
    
    // Copy original to undo
    FILE *src = fopen(filepath, "r");
    if (!src) return -1;
    
    FILE *dst = fopen(undo_path, "w");
    if (!dst) {
        fclose(src);
        return -1;
    }
    
    char buffer[MAX_BUFFER];
    size_t bytes;
    
    while ((bytes = fread(buffer, 1, sizeof(buffer), src)) > 0) {
        fwrite(buffer, 1, bytes, dst);
    }
    
    fclose(src);
    fclose(dst);
    return 0;
}

int restore_from_undo(const char *filepath) {
    char undo_path[MAX_PATH];
    snprintf(undo_path, sizeof(undo_path), "%s.undo", filepath);
    
    // Check if undo file exists
    if (access(undo_path, F_OK) != 0) {
        return -1;
    }
    
    // Copy undo to original
    FILE *src = fopen(undo_path, "r");
    if (!src) return -1;
    
    FILE *dst = fopen(filepath, "w");
    if (!dst) {
        fclose(src);
        return -1;
    }
    
    char buffer[MAX_BUFFER];
    size_t bytes;
    
    while ((bytes = fread(buffer, 1, sizeof(buffer), src)) > 0) {
        fwrite(buffer, 1, bytes, dst);
    }
    
    fclose(src);
    fclose(dst);
    
    // Remove undo file after restoration
    unlink(undo_path);
    return 0;
}

int undo_backup_exists(const char *filepath) {
    char undo_path[MAX_PATH];
    snprintf(undo_path, sizeof(undo_path), "%s.undo", filepath);
    return (access(undo_path, F_OK) == 0);
}