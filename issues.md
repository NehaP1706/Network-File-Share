# Known Issues & Fixes

## Critical Fixes Needed

### 1. EXEC Command - File Content Reading
**Issue**: In `nm.c`, the EXEC handler requests SS_INFO but needs actual file content.

**Fix**: Update `ss.c` to handle content request in `MSG_SS_INFO`:

```c
// In ss.c, handle_nm_communication()
case MSG_SS_INFO: {
    FileMetadata meta;
    if (strcmp(msg.data, "READ_CONTENT") == 0) {
        // Read file content
        char buffer[MAX_BUFFER];
        response.status = read_file_ss(msg.filename, buffer);
        if (response.status == SUCCESS) {
            strncpy(response.data, buffer, MAX_BUFFER - 1);
        }
    } else {
        // Regular info request
        response.status = get_file_info_ss(msg.filename, &meta);
        if (response.status == SUCCESS) {
            sprintf(response.data, "%zu|%d|%d|%ld|%ld", 
                    meta.size, meta.word_count, meta.char_count, 
                    meta.modified, meta.accessed);
        }
    }
    send_message(ss.nm_sock, &response);
    break;
}
```

### 2. File Metadata Initialization
**Issue**: When SS registers files, metadata has default "system" owner.

**Fix**: Either:
- Manual: Update file ownership after creation
- Automatic: First user to access becomes owner (requires tracking)

**Workaround**: Have each SS create files with proper metadata file.

### 3. Sentence Lock Array Bounds
**Issue**: If file has no sentences initially, lock array may be uninitialized.

**Fix**: In `ss.c`, ensure at least one sentence lock:

```c
void init_file_locks(const char *filename, int sentence_count) {
    if (sentence_count == 0) sentence_count = 1;  // At least one
    // ... rest of function
}
```

### 4. Multiple Delimiters Handling
**Issue**: Word like "Wait... what?" creates empty words.

**Fix**: In `file_ops.c`, filter empty words in `split_by_delimiters()`:

```c
// After creating part, check if empty
if (strlen(buffer) > 0) {
    parts[*count] = strdup(buffer);
    (*count)++;
}
```

## Minor Issues

### 5. Timestamp Formatting in INFO
**Issue**: `localtime_r()` is not thread-safe.

**Fix**: Use `localtime_r_r()` instead:

```c
struct tm tm_info;
localtime_r_r(&meta->accessed, &tm_info);
strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &tm_info);
```

### 6. Buffer Overflow in VIEW
**Issue**: Many files can overflow response buffer.

**Fix**: Paginate results or increase buffer:

```c
// Option 1: Send multiple messages
// Option 2: Dynamic allocation
char *buffer = malloc(file_count * 100);  // Estimate per file
```

### 7. SS Reconnection
**Issue**: If SS disconnects and reconnects, files may be duplicated in trie.

**Fix**: In `handle_ss_connection()`, check if files already exist before inserting:

```c
FileMetadata *existing = trie_search(nm.file_trie, token);
if (existing) {
    free(existing);
    continue;  // Skip already registered file
}
```

### 8. Cache Invalidation on Write
**Issue**: Cache may contain stale metadata after write operations.

**Fix**: Clear cache entry on write:

```c
// In handle_write() after successful write
cache_remove(nm.cache, msg->filename);
```

### 9. Heartbeat Thread Cleanup
**Issue**: Heartbeat thread may continue after SS shutdown.

**Fix**: Proper signal handling:

```c
void signal_handler(int sig) {
    ss.running = 0;
}

int main() {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    // ... rest of main
}
```

### 10. Word Index Confusion
**Issue**: Code uses 0-based internally but spec shows 1-based in examples.

**Current**: `insert_word_in_sentence()` converts 1-based to 0-based.

**Verify**: Test thoroughly that word_index=1 inserts at start.

## Testing Checklist

- [ ] Create file as user1, verify ownership
- [ ] Write with multiple delimiters in one word
- [ ] Write to empty file (no sentences yet)
- [ ] Concurrent writes to same sentence
- [ ] Concurrent writes to different sentences
- [ ] UNDO immediately after write
- [ ] UNDO with no previous changes
- [ ] DELETE file you don't own
- [ ] WRITE to file with only READ access
- [ ] STREAM while SS goes down mid-stream
- [ ] VIEW with 100+ files (buffer test)
- [ ] EXEC with multiline script
- [ ] EXEC with invalid commands
- [ ] Start client before NM (should fail gracefully)
- [ ] Start SS before NM (should fail gracefully)
- [ ] Kill SS, verify NM marks as inactive
- [ ] Restart SS, verify files re-register

## Performance Issues

### 11. Linear Search in SS List
**Issue**: Finding SS by ID is O(n).

**Fix**: Use hashmap or keep SS list sorted.

### 12. Cache Size
**Issue**: Fixed 100 entries may be insufficient.

**Fix**: Make configurable or implement adaptive sizing.

### 13. Lock Contention
**Issue**: Global ss_mutex in NM can be bottleneck.

**Fix**: Use finer-grained locks (per-SS, per-client).

## Memory Leaks

### 14. FileMetadata Allocation
**Issue**: Multiple allocations of FileMetadata may leak.

**Fix**: Audit all `trie_search()`, `cache_get()` calls ensure `free()`.

### 15. Thread Resource Leaks
**Issue**: Detached threads with malloc'd arguments.

**Fix**: Already handled with `pthread_detach()` but verify cleanup.

## Security Issues

### 16. Command Injection in EXEC
**Issue**: Direct shell execution without sanitization.

**Fix**: Use `execvp()` with argument array instead of `popen()`.

### 17. Path Traversal
**Issue**: Filename like "../../../etc/passwd" could escape storage.

**Fix**: Validate filenames:

```c
int is_valid_filename(const char *name) {
    if (strstr(name, "..") || strchr(name, '/') || strchr(name, '\\')) {
        return 0;
    }
    return 1;
}
```

### 18. Buffer Overflows
**Issue**: `strncpy()` used but not always null-terminated.

**Fix**: Always null-terminate:

```c
strncpy(dest, src, MAX_SIZE - 1);
dest[MAX_SIZE - 1] = '\0';
```

## Recommended Improvements

1. **Add timeout to socket operations** - Prevent indefinite blocking
2. **Implement proper shutdown** - Graceful cleanup on Ctrl+C
3. **Add file size limits** - Prevent memory exhaustion
4. **Implement SS selection policy** - Least-loaded instead of round-robin
5. **Add metadata persistence** - Store trie to disk for NM restart
6. **Version undo history** - Multiple undo levels
7. **Add file locking timeout** - Auto-release after N minutes
8. **Implement proper logging levels** - DEBUG/INFO/WARN/ERROR filtering
9. **Add configuration file** - Ports, limits, paths
10. **Unit tests** - Test individual components

## Debugging Tips

### Enable Verbose Logging
Add debug prints:

```c
#define DEBUG 1
#if DEBUG
    printf("[DEBUG] Variable X = %d\n", x);
#endif
```

### Use Valgrind
Check memory leaks:

```bash
valgrind --leak-check=full ./nm
valgrind --leak-check=full ./ss 127.0.0.1 8080 9001
```

### Use GDB
Debug crashes:

```bash
gdb ./nm
(gdb) run
(gdb) bt  # backtrace on crash
```

### Check Logs
All operations logged:

```bash
tail -f nm.log
tail -f ss_*.log
```

### Network Debugging
Use netcat to test:

```bash
# Test if NM is listening
nc -zv 127.0.0.1 8081

# Monitor traffic
tcpdump -i lo port 8080
```

## Common Error Messages

| Error | Cause | Solution |
|-------|-------|----------|
| "Connection refused" | NM not running | Start NM first |
| "Bind failed" | Port in use | Kill process or use different port |
| "Sentence locked" | Concurrent write | Wait for other user to finish |
| "Access denied" | No permissions | Request access from owner |
| "File not found" | Invalid filename | Check spelling, use VIEW |
| "Invalid index" | Out of bounds | Check sentence/word count |
| "SS unavailable" | No SS connected | Start at least one SS |

## Future Work

### Bonus Features Implementation
1. **Hierarchical Folders**: Add `parent_id` to FileMetadata
2. **Checkpoints**: Store multiple undo versions with tags
3. **Request Access**: Add pending_requests queue in NM
4. **Replication**: Maintain replica_ss_ids list per file
5. **Fault Tolerance**: Health checks, automatic failover

### Architecture Improvements
1. **Separate metadata store**: Database instead of trie
2. **Message queue**: Decouple request/response
3. **Load balancer**: Separate component for SS selection
4. **Authentication server**: Separate user management
5. **Monitoring dashboard**: Real-time system stats

## Conclusion

This implementation covers all required functionality but needs:
- **Thorough testing** of edge cases
- **Security hardening** for production use
- **Performance optimization** for scale
- **Proper error recovery** mechanisms

Test systematically using the checklist above before submission!
