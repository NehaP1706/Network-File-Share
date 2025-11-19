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
```
┌─────────────────────────────────────────────────────────────────────┐
│                        NETWORK FILE SYSTEM                          │
└─────────────────────────────────────────────────────────────────────┘

              ┌──────────────────────────────────┐
              │       Name Server (NM)           │
              │  IP: Known publicly              │
              │  Ports: 8080 (SS), 8081 (Client) │
              │        8082 (Heartbeat)          │
              └──────────────────────────────────┘
                         ▲  ▲  ▲
                         │  │  │
         ┌───────────────┘  │  └────────────────┐
         │                  │                   │
         │ Registration     │ Heartbeat         │ Registration
         │ Commands         │ (Port 8082)       │ Requests
         │                  │                   │
         ▼                  ▼                   ▼
┌─────────────────┐  ┌─────────────────┐  ┌──────────────┐
│ Storage Server 1│  │ Storage Server 2│  │   Client 1   │
│ Ports:          │  │ Ports:          │  │  Username:   │
│  NM: 8080       │  │  NM: 8080       │  │  alice       │
│  Client: 9001   │  │  Client: 9002   │  └──────────────┘
│  HB: 8082       │  │  HB: 8082       │
└─────────────────┘  └─────────────────┘  ┌──────────────┐
         │                   │             │   Client 2   │
         │                   │             │  Username:   │
         │ Direct Client     │             │  bob         │
         │ Connection        │             └──────────────┘
         │ (Read/Write/      │
         │  Stream)          │
         └───────────────────┘
```
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
```
```
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
```
```
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
```
```
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
```
```
┌────────────────────────────────────────┐
│       FileContent Structure             │
├─────────────────────────────────────────┤
│ sentences: Sentence*                    │
│ sentence_count: int                     │
│ capacity: int                           │
└─────────────────────────────────────────┘
            │
            ├─── Sentence[0]
            │    ┌──────────────────────────┐
            │    │ words: char**            │
            │    │ word_count: int          │
            │    │ capacity: int            │
            │    └──────────────────────────┘
            │         │
            │         ├─── words[0]: "Hello"
            │         ├─── words[1]: " "
            │         ├─── words[2]: "world"
            │         └─── words[3]: "."
            │
            ├─── Sentence[1]
            │    ┌──────────────────────────┐
            │    │ words: char**            │
            │    │ word_count: int          │
            │    │ capacity: int            │
            │    └──────────────────────────┘
            │         │
            │         ├─── words[0]: "How"
            │         ├─── words[1]: " "
            │         ├─── words[2]: "are"
            │         ├─── words[3]: " "
            │         ├─── words[4]: "you"
            │         └─── words[5]: "?"
            └─── ...

Note: Words include delimiters (., !, ?) and whitespace (" ", "\n", "\t") as separate tokens
```
```
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
```
```
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
```
```
┌────────┐                  ┌────────┐                  ┌────────┐
│ Client │                  │   NM   │                  │   SS   │
└───┬────┘                  └───┬────┘                  └───┬────┘
    │                           │                           │
    │ CREATE <filename>         │                           │
    ├──────────────────────────>│                           │
    │                           │                           │
    │                           │ Check if file exists      │
    │                           │ in Trie                   │
    │                           │                           │
    │                           │ Select SS (round-robin)   │
    │                           │                           │
    │                           │ CREATE <filename>         │
    │                           ├──────────────────────────>│
    │                           │                           │
    │                           │                           │ Create empty file
    │                           │                           │ on disk
    │                           │                           │
    │                           │        ACK (SUCCESS)      │
    │                           │<──────────────────────────┤
    │                           │                           │
    │                           │ Add to Trie:              │
    │                           │  - Insert FileMetadata    │
    │                           │  - Set owner to sender    │
    │                           │  - Set ss_id              │
    │                           │  - Init timestamps        │
    │                           │                           │
    │        ACK (SUCCESS)      │                           │
    │<──────────────────────────┤                           │
    │                           │                           │
    │ "File created!"           │                           │
    │                           │                           │
```
```
┌────────┐                  ┌────────┐                  ┌────────┐
│ Client │                  │   NM   │                  │   SS   │
└───┬────┘                  └───┬────┘                  └───┬────┘
    │                           │                           │
    │ READ <filename>           │                           │
    ├──────────────────────────>│                           │
    │                           │                           │
    │                           │ Check access (READ)       │
    │                           │  - Search Trie            │
    │                           │  - Verify ACL             │
    │                           │                           │
    │                           │ Find SS from Trie         │
    │                           │                           │
    │  SS Info: "IP:PORT"       │                           │
    │<──────────────────────────┤                           │
    │                           │                           │
    │                           │                           │
    │  READ <filename>          │                           │
    ├───────────────────────────────────────────────────────>│
    │                           │                           │
    │                           │                           │ Read file from disk
    │                           │                           │ Parse content
    │                           │                           │
    │        File Content       │                           │
    │<───────────────────────────────────────────────────────┤
    │                           │                           │
    │ Display content           │                           │
    │                           │                           │
```
```
┌────────┐                  ┌────────┐                  ┌────────┐
│ Client │                  │   NM   │                  │   SS   │
└───┬────┘                  └───┬────┘                  └───┬────┘
    │                           │                           │
    │ WRITE <file> <sent_idx>   │                           │
    ├──────────────────────────>│                           │
    │                           │                           │
    │                           │ Check access (WRITE)      │
    │                           │  - Search Trie            │
    │                           │  - Verify ACL             │
    │                           │                           │
    │  SS Info: "IP:PORT"       │                           │
    │<──────────────────────────┤                           │
    │                           │                           │
    │  LOCK_SENTENCE            │                           │
    │  <file> <sent_idx>        │                           │
    ├───────────────────────────────────────────────────────>│
    │                           │                           │
    │                           │                           │ Validate sent_idx
    │                           │                           │ Check if locked
    │                           │                           │
    │                           │                           │ Lock acquired:
    │                           │                           │  - Set lock flag
    │                           │                           │  - Record username
    │                           │                           │  - Store lock_time
    │                           │                           │
    │                           │                           │ Start write session:
    │                           │                           │  - Copy file to temp
    │                           │                           │  - Store original_count
    │                           │                           │
    │        ACK (SUCCESS)      │                           │
    │<───────────────────────────────────────────────────────┤
    │                           │                           │
    │ "Sentence locked"         │                           │
    │                           │                           │
    │ WRITE <word_idx> <content>│                           │
    ├───────────────────────────────────────────────────────>│
    │                           │                           │
    │                           │                           │ Parse temp file
    │                           │                           │ Insert word at position
    │                           │                           │ Handle delimiters
    │                           │                           │ Write back to temp
    │                           │                           │
    │        ACK (SUCCESS)      │                           │
    │<───────────────────────────────────────────────────────┤
    │                           │                           │
    │ (User can repeat writes)  │                           │
    │                           │                           │
    │ ETIRW (UNLOCK)            │                           │
    ├───────────────────────────────────────────────────────>│
    │                           │                           │
    │                           │                           │ Commit write session:
    │                           │                           │  - Add to commit queue
    │                           │                           │  - Process queue (FIFO)
    │                           │                           │  - Merge temp → main
    │                           │                           │  - Adjust indices
    │                           │                           │
    │                           │                           │ Unlock sentence
    │                           │                           │
    │        ACK (SUCCESS)      │                           │
    │<───────────────────────────────────────────────────────┤
    │                           │                           │
    │ "Write successful!"       │                           │
    │                           │                           │
```
```
┌────────┐                  ┌────────┐                  ┌────────┐
│ Client │                  │   NM   │                  │   SS   │
└───┬────┘                  └───┬────┘                  └───┬────┘
    │                           │                           │
    │ STREAM <filename>         │                           │
    ├──────────────────────────>│                           │
    │                           │                           │
    │                           │ Check access (READ)       │
    │                           │                           │
    │  SS Info: "IP:PORT"       │                           │
    │<──────────────────────────┤                           │
    │                           │                           │
    │  STREAM <filename>        │                           │
    ├───────────────────────────────────────────────────────>│
    │                           │                           │
    │                           │                           │ Parse file
    │                           │                           │
    │        ACK                │                           │
    │<───────────────────────────────────────────────────────┤
    │                           │                           │
    │                           │                           │ For each word:
    │        Word 1             │                           │
    │<───────────────────────────────────────────────────────┤
    │ Display + space           │                           │
    │                           │                           │ usleep(100000)
    │        Word 2             │                           │ [0.1 sec delay]
    │<───────────────────────────────────────────────────────┤
    │ Display + space           │                           │
    │                           │                           │
    │         ...               │                           │
    │                           │                           │
    │        Word N             │                           │
    │<───────────────────────────────────────────────────────┤
    │ Display                   │                           │
    │                           │                           │
    │        STOP               │                           │
    │<───────────────────────────────────────────────────────┤
    │                           │                           │
    │ "\n"                      │                           │
    │                           │                           │

Note: If SS crashes mid-stream, client detects connection loss and displays error
```
```
┌────────┐                  ┌────────┐                  ┌────────┐
│ Client │                  │   NM   │                  │   SS   │
└───┬────┘                  └───┬────┘                  └───┬────┘
    │                           │                           │
    │ DELETE <filename>         │                           │
    ├──────────────────────────>│                           │
    │                           │                           │
    │                           │ Check ownership           │
    │                           │  - Search Trie            │
    │                           │  - Verify sender is owner │
    │                           │                           │
    │                           │ Find SS                   │
    │                           │                           │
    │                           │ CHECK_LOCKS               │
    │                           ├──────────────────────────>│
    │                           │                           │
    │                           │                           │ Check all sentence locks
    │                           │                           │ for this file
    │                           │                           │
    │                           │  ERR_FILE_LOCKED or SUCCESS
    │                           │<──────────────────────────┤
    │                           │                           │
    │                           │ If locked:                │
    │  ERR_FILE_LOCKED          │  - Return error           │
    │<──────────────────────────┤                           │
    │                           │                           │
    │                           │ If not locked:            │
    │                           │ DELETE <filename>         │
    │                           ├──────────────────────────>│
    │                           │                           │
    │                           │                           │ Delete file from disk
    │                           │                           │ Delete .undo file
    │                           │                           │
    │                           │        ACK (SUCCESS)      │
    │                           │<──────────────────────────┤
    │                           │                           │
    │                           │ Remove from Trie          │
    │                           │ Remove from Cache         │
    │                           │                           │
    │        ACK (SUCCESS)      │                           │
    │<──────────────────────────┤                           │
    │                           │                           │
    │ "File deleted!"           │                           │
    │                           │                           │
```
```
┌────────┐                  ┌────────┐
│ Client │                  │   NM   │
└───┬────┘                  └───┬────┘
    │                           │
    │ ADDACCESS -W file user    │
    ├──────────────────────────>│
    │                           │
    │                           │ Verify ownership
    │                           │  - Search Trie
    │                           │  - Check sender == owner
    │                           │
    │                           │ Check if user exists
    │                           │  - Search registered_users[]
    │                           │
    │                           │ Update FileMetadata:
    │                           │  - Add to ACL array
    │                           │  - Set access level
    │                           │
    │                           │ Update Trie
    │                           │ Update Cache
    │                           │
    │        ACK (SUCCESS)      │
    │<──────────────────────────┤
    │                           │
    │ "Access granted!"         │
    │                           │
```
```
┌────────┐                  ┌────────┐
│ Client │                  │   NM   │
└───┬────┘                  └───┬────┘
    │                           │
    │ REQUESTACCESS -R file     │
    ├──────────────────────────>│
    │                           │
    │                           │ Check if file exists
    │                           │
    │                           │ Add to access_requests[]:
    │                           │  - username
    │                           │  - filename
    │                           │  - requested_access
    │                           │  - request_time
    │                           │
    │        ACK (SUCCESS)      │
    │<──────────────────────────┤
    │                           │
    │ "Request sent!"           │
    │                           │
```
```
┌────────┐                  ┌────────┐
│ Owner  │                  │   NM   │
└───┬────┘                  └───┬────┘
    │                           │
    │ VIEWREQUESTS              │
    ├──────────────────────────>│
    │                           │
    │                           │ Filter access_requests[]
    │                           │  - Find requests where
    │                           │    file.owner == sender
    │                           │
    │  List of requests         │
    │  [ID] user, file, access  │
    │<──────────────────────────┤
    │                           │
    │ APPROVEREQUEST <id>       │
    ├──────────────────────────>│
    │                           │
    │                           │ Verify ownership
    │                           │ Grant access (update ACL)
    │                           │ Remove from queue
    │                           │
    │        ACK (SUCCESS)      │
    │<──────────────────────────┤
    │                           │
```
```
┌────────┐                  ┌────────┐                  ┌────────┐
│ Client │                  │   NM   │                  │   SS   │
└───┬────┘                  └───┬────┘                  └───┬────┘
    │                           │                           │
    │ INFO <filename>           │                           │
    ├──────────────────────────>│                           │
    │                           │                           │
    │                           │ Search Trie               │
    │                           │  - Get FileMetadata       │
    │                           │                           │
    │                           │ SS_INFO request           │
    │                           ├──────────────────────────>│
    │                           │                           │
    │                           │                           │ stat() file
    │                           │                           │ get_file_stats()
    │                           │                           │  - word_count
    │                           │                           │  - char_count
    │                           │                           │
    │                           │  Updated metadata         │
    │                           │<──────────────────────────┤
    │                           │                           │
    │                           │ Update Trie               │
    │                           │ Update Cache              │
    │                           │                           │
    │                           │ Format response:          │
    │                           │  - Filename               │
    │                           │  - Owner                  │
    │                           │  - Created timestamp      │
    │                           │  - Modified timestamp     │
    │                           │  - Accessed timestamp     │
    │                           │  - Last accessed by       │
    │                           │  - Size (bytes)           │
    │                           │  - Word count             │
    │                           │  - Char count             │
    │                           │  - Storage Server ID      │
    │                           │  - ACL entries            │
    │                           │                           │
    │  Formatted info           │                           │
    │<──────────────────────────┤                           │
    │                           │                           │
    │ Display info              │                           │
    │                           │                           │

```
┌────────┐                  ┌────────┐                  ┌────────┐
│ Client │                  │   NM   │                  │   SS   │
└───┬────┘                  └───┬────┘                  └───┬────┘
    │                           │                           │
    │ VIEW [-a] [-l]            │                           │
    ├──────────────────────────>│                           │
    │                           │                           │
    │                           │ Parse flags:              │
    │                           │  -a: show all files       │
    │                           │  -l: show details         │
    │                           │                           │
    │                           │ Get all files from Trie   │
    │                           │  trie_get_all_files()     │
    │                           │                           │
    │                           │ If -l flag:               │
    │                           │   For each file:          │
    │                           │     SS_INFO <file>        │
    │                           │   ├─────────────────────>│
    │                           │   │                      │
    │                           │   │  Updated stats       │
    │                           │   │<─────────────────────┤
    │                           │   │                      │
    │                           │   Update Trie & Cache    │
    │                           │                           │
    │                           │ Filter files:             │
    │                           │  If -a: show all          │
    │                           │  Else: check access       │
    │                           │                           │
    │                           │ Format output:            │
    │                           │  Simple: filenames        │
    │                           │  Detailed: table format   │
    │                           │                           │
    │  File list                │                           │
    │<──────────────────────────┤                           │
    │                           │                           │
    │ Display list              │                           │
    │                           │                           │
```
```
┌────────┐                  ┌────────┐                  ┌────────┐
│ Client │                  │   NM   │                  │   SS   │
└───┬────┘                  └───┬────┘                  └───┬────┘
    │                           │                           │
    │ UNDO <filename>           │                           │
    ├──────────────────────────>│                           │
    │                           │                           │
    │                           │ Check access (WRITE)      │
    │                           │                           │
    │  SS Info: "IP:PORT"       │                           │
    │<──────────────────────────┤                           │
    │                           │                           │
    │  UNDO <filename>          │                           │
    ├───────────────────────────────────────────────────────>│
    │                           │                           │
    │                           │                           │ Check if .undo exists
    │                           │                           │  filepath + ".undo"
    │                           │                           │
    │                           │                           │ If exists:
    │                           │                           │  - Copy .undo → main
    │                           │                           │  - Delete .undo file
    │                           │                           │
    │                           │                           │ If not exists:
    │                           │                           │  - Return error
    │                           │                           │
    │        ACK (SUCCESS/ERR)  │                           │
    │<───────────────────────────────────────────────────────┤
    │                           │                           │
    │ Display result            │                           │
    │                           │                           │

Note: .undo file is created before any write operation
      Only one level of undo is supported
```
```
┌──────────────────────────────────────────────────────────┐
│              Folder Hierarchy Example                    │
└──────────────────────────────────────────────────────────┘

Storage Structure:
/
├── file1.txt
├── file2.txt
├── projects/
│   ├── report.txt
│   ├── data.txt
│   └── research/
│       ├── notes.txt
│       └── analysis.txt
└── documents/
    └── draft.txt

┌──────────────────────────────────────────────────────────┐
│ CREATEFOLDER Operation:                                  │
├──────────────────────────────────────────────────────────┤
│ Client → NM:                                             │
│   CREATEFOLDER projects                                  │
│                                                          │
│ NM:                                                      │
│   1. Check if folder exists in FolderTrie                │
│   2. Select SS (round-robin)                             │
│   3. Forward to SS                                       │
│                                                          │
│ SS:                                                      │
│   4. mkdir(ss_storage/projects)                          │
│   5. Return SUCCESS                                      │
│                                                          │
│ NM:                                                      │
│   6. Insert into FolderTrie:                             │
│      - path: "/projects"                                 │
│      - owner: sender                                     │
│      - ss_id: selected_ss                                │
│      - created: timestamp                                │
└──────────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────────┐
│ MOVE Operation:                                          │
├──────────────────────────────────────────────────────────┤
│ Client → NM:                                             │
│   MOVE report.txt projects                               │
│                                                          │
│ NM:                                                      │
│   1. Check file ownership/access                         │
│   2. Verify target folder exists                         │
│   3. Get file's ss_id from Trie                          │
│   4. Forward to SS with old_path and new_path            │
│                                                          │
│ SS:                                                      │
│   5. rename(ss_storage/report.txt,                       │
│             ss_storage/projects/report.txt)              │
│   6. Move .undo file if exists                           │
│   7. Return SUCCESS                                      │
│                                                          │
│ NM:                                                      │
│   8. Update FileMetadata in Trie:                        │
│      - folder_path: "/projects"                          │
│   9. Update Cache                                        │
└──────────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────────┐
│ VIEWFOLDER Operation:                                    │
├──────────────────────────────────────────────────────────┤
│ Client → NM:                                             │
│   VIEWFOLDER projects                                    │
│                                                          │
│ NM:                                                      │
│   1. Get all files from Trie                             │
│   2. Filter files where:                                 │
│      - folder_path == "/projects"                        │
│      - user has READ access                              │
│   3. Return list                                         │
│                                                          │
│ Output:                                                  │
│   report.txt                                             │
│   data.txt                                               │
└──────────────────────────────────────────────────────────┘
```
```
┌──────────────────────────────────────────────────────────┐
│              Checkpoint System                           │
└──────────────────────────────────────────────────────────┘

File Timeline:
┌──────────────────────────────────────────────────────────┐
│ document.txt                                             │
│   Version 1: "Initial content."                          │
│        ↓                                                 │
│   [CHECKPOINT "v1"]  ← Saved as document.txt.checkpoint_v1
│        ↓                                                 │
│   Version 2: "Initial content. Added more."              │
│        ↓                                                 │
│   [CHECKPOINT "draft"]  ← document.txt.checkpoint_draft  │
│        ↓                                                 │
│   Version 3: "Initial content. Added more. Final."       │
│        ↓                                                 │
│   [CHECKPOINT "final"]  ← document.txt.checkpoint_final  │
└──────────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────────┐
│ CHECKPOINT Operation:                                    │
├──────────────────────────────────────────────────────────┤
│ Client → NM:                                             │
│   CHECKPOINT document.txt v1                             │
│                                                          │
│ NM:                                                      │
│   1. Check WRITE access                                  │
│   2. Find SS from Trie                                   │
│   3. Forward to SS                                       │
│                                                          │
│ SS:                                                      │
│   4. Check if checkpoint already exists                  │
│   5. Copy file:                                          │
│      cp ss_storage/document.txt \                        │
│         ss_storage/document.txt.checkpoint_v1            │
│   6. Return SUCCESS                                      │
└──────────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────────┐
│ LISTCHECKPOINTS Operation:                              │
├──────────────────────────────────────────────────────────┤
│ Client → NM:                                             │
│   LISTCHECKPOINTS document.txt                           │
│                                                          │
│ NM → SS:                                                 │
│   Forward request                                        │
│                                                          │
│ SS:                                                      │
│   1. opendir(ss_storage)                                 │
│   2. Find files matching "document.txt.checkpoint_*"     │
│   3. Extract tag names                                   │
│   4. Return list                                         │
│                                                          │
│ Output:                                                  │
│   v1                                                     │
│   draft                                                  │
│   final                                                  │
└──────────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────────┐
│ REVERT Operation:                                        │
├──────────────────────────────────────────────────────────┤
│ Client → NM:                                             │
│   REVERT document.txt draft                              │
│                                                          │
│ NM:                                                      │
│   1. Check WRITE access                                  │
│   2. Forward to SS                                       │
│                                                          │
│ SS:                                                      │
│   3. Create undo backup:                                 │
│      cp document.txt document.txt.undo                   │
│   4. Restore checkpoint:                                 │
│      cp document.txt.checkpoint_draft document.txt       │
│   5. Return SUCCESS                                      │
│                                                          │
│ Result: File reverted to "draft" checkpoint              │
│         Previous version saved in .undo                  │
└──────────────────────────────────────────────────────────┘
```