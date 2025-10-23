#ifndef TRIE_H
#define TRIE_H

#include "common.h"

#define ALPHABET_SIZE 128  // ASCII

typedef struct TrieNode {
    struct TrieNode *children[ALPHABET_SIZE];
    int is_end_of_word;
    FileMetadata *file_meta;  // Only set if is_end_of_word is true
} TrieNode;

typedef struct {
    TrieNode *root;
    pthread_rwlock_t lock;
} Trie;

// Initialize trie
Trie* init_trie();

// Free trie
void free_trie(Trie *trie);

// Insert file metadata into trie
int trie_insert(Trie *trie, const char *filename, FileMetadata *meta);

// Search for file in trie
FileMetadata* trie_search(Trie *trie, const char *filename);

// Delete file from trie
int trie_delete(Trie *trie, const char *filename);

// Update file metadata in trie
int trie_update(Trie *trie, const char *filename, FileMetadata *meta);

// Get all files (for listing)
int trie_get_all_files(Trie *trie, FileMetadata **files, int max_files);

#endif // TRIE_H
