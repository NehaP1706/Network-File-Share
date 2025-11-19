[![Review Assignment Due Date](https://classroom.github.com/assets/deadline-readme-button-22041afd0340ce965d47ae6ef1cefeee28c7c493a6346c4f15d667ab976d596c.svg)](https://classroom.github.com/a/0ek2UV58)

DIRECTORY STRUCTURE:

```c
course-project-shellshocked/
├── cache.c
├── cache.h
├── client.c
├── common.c
├── common.h
├── file_ops.c
├── file_ops.h
├── logger.c
├── logger.h
├── Makefile
├── nm.c
├── README.md
├── ss.c
├── trie.c
└── trie.h

1 directory, 15 files
```
┌─────────────────────────────────────────┐
│           Message Structure             │
├─────────────────────────────────────────┤
│ type: MessageType                       │
│ status: int                             │
│ sender: char[MAX_USERNAME]              │
│ filename: char[MAX_FILENAME]            │
│ foldername: char[MAX_FILENAME]          │
│ checkpoint_tag: char[MAX_USERNAME]      │
│ target_path: char[MAX_PATH]             │
│ data: char[MAX_BUFFER]                  │
│ sentence_index: int                     │
│ word_index: int                         │
│ ss_id: int                              │
│ client_port: int                        │
│ nm_port: int                            │
│ access: AccessType                      │
│ target_user: char[MAX_USERNAME]         │
└─────────────────────────────────────────┘

Trie Structure:
┌─────────────────────────────────────────────┐
│              Trie Root Node                 │
└─────────────────────────────────────────────┘
              │
              ├─── 'f' ───┐
              │           │
              │           ├─── 'i' ───┐
              │           │           │
              │           │           ├─── 'l' ───┐
              │           │           │           │
              │           │           │           ├─── 'e' ───┐
              │           │           │           │           │
              │           │           │           │      [FileMetadata*]
              │           │           │           │      is_end_of_word=1
              │
              ├─── 't' ───┐
                          │
                          ├─── 'e' ───┐
                          │           │
                          │           ├─── 's' ───┐
                          │           │           │
                          │           │           ├─── 't' ───┐
                          │           │           │           │
                          │           │           │      [FileMetadata*]
                          │           │           │      is_end_of_word=1

Each node contains:
┌─────────────────────────────────┐
│       TrieNode Structure        │
├─────────────────────────────────┤
│ children[ALPHABET_SIZE=128]     │
│ is_end_of_word: int             │
│ file_meta: FileMetadata*        │
└─────────────────────────────────┘

LRU Cache Structure:

┌────────────────────────────────────────────────────────────┐
│                      LRUCache                              │
├────────────────────────────────────────────────────────────┤
│ head ──→ [Dummy] ←─→ [Node1] ←─→ [Node2] ←─→ [Dummy] ←── tail
│                         ↑                                  │
│                    Most Recently Used                      │
│                                                            │
│ hash_table[capacity]:                                      │
│   [0] → NULL                                               │
│   [1] → Node2                                              │
│   [2] → NULL                                               │
│   ...                                                      │
│   [hash(key)] → Node1                                      │
│                                                            │
│ capacity: int                                              │
│ size: int                                                  │
│ lock: pthread_mutex_t                                      │
└────────────────────────────────────────────────────────────┘

CacheNode Structure:
┌─────────────────────────────────┐
│      CacheNode                  │
├─────────────────────────────────┤
│ key: char[MAX_FILENAME]         │
│ value: FileMetadata*            │
│ prev: CacheNode*                │
│ next: CacheNode*                │
└─────────────────────────────────┘

Operations:
- cache_get(): O(1) - Moves accessed node to head
- cache_put(): O(1) - Adds/updates and moves to head
- Eviction: Removes tail node when capacity exceeded

┌─────────────────────────────────────────┐
│       FileMetadata Structure            │
├─────────────────────────────────────────┤
│ filename: char[MAX_FILENAME]            │
│ folder_path: char[MAX_PATH]             │
│ owner: char[MAX_USERNAME]               │
│ ss_id: int                              │
│ size: size_t                            │
│ word_count: int                         │
│ char_count: int                         │
│ created: time_t                         │
│ modified: time_t                        │
│ accessed: time_t                        │
│ last_accessed_by: char[MAX_USERNAME]    │
│ acl[MAX_ACL_ENTRIES]:                   │
│   ┌─────────────────────────────┐       │
│   │ username: char[MAX_USERNAME]│       │
│   │ access: AccessType          │       │
│   └─────────────────────────────┘       │
│ acl_count: int                          │
└─────────────────────────────────────────┘

Write Session Flow:

┌──────────────────────────────────────────────────────────┐
│                  Write Session                           │
├──────────────────────────────────────────────────────────┤
│ filename: char[MAX_FILENAME]                             │
│ username: char[MAX_USERNAME]                             │
│ sentence_idx: int                                        │
│ temp_filepath: char[MAX_PATH]                            │
│ active: int                                              │
│ original_sentence_count: int                             │
│ lock_time: time_t                                        │
└──────────────────────────────────────────────────────────┘
                       ↓
              Creates temp file
                       ↓
┌──────────────────────────────────────────────────────────┐
│            file.txt.temp_user_sentidx                    │
│  (Isolated workspace for user's modifications)           │
└──────────────────────────────────────────────────────────┘
                       ↓
              User makes writes
                       ↓
              UNLOCK/COMMIT
                       ↓
┌──────────────────────────────────────────────────────────┐
│              Commit Queue Entry                          │
├──────────────────────────────────────────────────────────┤
│ Queued based on lock_time (FIFO order)                   │
│ - filename                                               │
│ - username                                               │
│ - sentence_idx                                           │
│ - original_sentence_count                                │
│ - temp_filepath                                          │
│ - lock_time                                              │
└──────────────────────────────────────────────────────────┘
                       ↓
              Sequential Processing
                       ↓
┌──────────────────────────────────────────────────────────┐
│              Merge Algorithm                             │
├──────────────────────────────────────────────────────────┤
│ 1. Calculate sentence shift                              │
│ 2. Adjust target sentence index                          │
│ 3. Merge temp content into main file                     │
│ 4. Write back to disk                                    │
│ 5. Clean up temp file                                    │
└──────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────┐
│       File Lock Structure               │
├─────────────────────────────────────────┤
│ filename: char[MAX_FILENAME]            │
│ locks[lock_count]:                      │
│   ┌─────────────────────────────┐       │
│   │    SentenceLock             │       │
│   ├─────────────────────────────┤       │
│   │ locked: int                 │       │
│   │ locked_by: char[MAX_USERNAME]│      │
│   │ lock_time: time_t           │       │
│   │ mutex: pthread_mutex_t      │       │
│   └─────────────────────────────┘       │
│ lock_count: int                         │
└─────────────────────────────────────────┘

Per-Sentence Locking:
Sentence 0: [UNLOCKED] ───────────────────
Sentence 1: [LOCKED by user_A] ───────────
Sentence 2: [UNLOCKED] ───────────────────
Sentence 3: [LOCKED by user_B] ───────────

