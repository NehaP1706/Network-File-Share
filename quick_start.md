# Quick Start Guide - 5 Minutes to Running System

## Prerequisites
- Linux/Unix environment
- GCC compiler
- Make utility
- 4 terminal windows

## Step 1: Build (30 seconds)
```bash
cd project_directory
make clean && make all
```

Expected output:
```
gcc -Wall -Wextra -pthread -g -c common.c
gcc -Wall -Wextra -pthread -g -c logger.c
...
[Success messages]
```

Verify executables exist:
```bash
ls -lh nm ss client
```

## Step 2: Start Name Server (Terminal 1)
```bash
./nm
```

Expected output:
```
[NM] Name Server initialized
[NM] SS Port: 8080
[NM] Client Port: 8081
[NM] Listening for Storage Servers on port 8080
[NM] Listening for Clients on port 8081
[NM] Name Server running. Press Ctrl+C to stop.
```

✅ **Leave this running**

## Step 3: Start Storage Server (Terminal 2)
```bash
./ss 127.0.0.1 8080 9001
```

Expected output:
```
[SS 12345] Storage Server initialized
[SS 12345] Storage path: ./ss_storage_12345
[SS 12345] Client port: 9001
[SS 12345] Connected to Name Server at 127.0.0.1:8080
[SS 12345] Registered 0 files with NM
[SS 12345] Listening for clients on port 9001
[SS 12345] Storage Server running. Press Ctrl+C to stop.
```

✅ **Leave this running**

## Step 4: Start Client (Terminal 3)
```bash
./client
```

Enter username when prompted:
```
Enter username: alice
[Client] Username: alice
[Client] Connected to Name Server

Welcome alice! Type commands (or 'help' for list, 'exit' to quit):

>
```

## Step 5: Try Basic Operations (2 minutes)

### Create a file
```
> CREATE hello.txt
File created successfully!
```

### Write to the file
```
> WRITE hello.txt 0
Sentence locked. Enter writes (word_index content), then type ETIRW:
Client: 1 Hello
Client: 2 World.
Client: ETIRW
Write successful!
```

### Read the file
```
> READ hello.txt
Hello World.
```

### View all files
```
> VIEW
hello.txt
```

### Get file info
```
> INFO hello.txt
File: hello.txt
Owner: alice
Created: 2024-12-XX XX:XX:XX
Last Modified: 2024-12-XX XX:XX:XX
Last Accessed: 2024-12-XX XX:XX:XX by alice
Size: 13 bytes
Words: 2
Chars: 13
Storage Server: 12345
Access Control:
```

### Exit
```
> exit
Goodbye!
```

## Step 6: Test Multi-User (Terminal 4)

Start second client:
```bash
./client
```

Enter different username:
```
Enter username: bob
```

Try to read alice's file:
```
> READ hello.txt
Error: Access denied
```

Go back to alice's terminal (Terminal 3):
```
> ADDACCESS -R hello.txt bob
Access granted successfully!
```

Now bob can read (Terminal 4):
```
> READ hello.txt
Hello World.
```

## 🎉 Success! System is working!

## Common First-Time Issues

### "Connection refused"
**Cause:** NM not started first  
**Fix:** Start NM before SS and clients

### "Bind: Address already in use"
**Cause:** Previous instance still running  
**Fix:**
```bash
# Find and kill process
lsof -i :8080  # or :8081, :9001
kill <PID>

# Or use different ports
./ss 127.0.0.1 8080 9002  # Use 9002 instead
```

### "File not found"
**Cause:** File doesn't exist or typo  
**Fix:** Use `VIEW` to see available files

### Weird characters in output
**Cause:** Binary data or encoding issue  
**Fix:** Only use text files, ASCII content

## Next Steps

### Try More Features
```
> help                          # See all commands
> STREAM hello.txt              # Word-by-word display
> UNDO hello.txt                # Revert changes
> LIST                          # See all users
> DELETE hello.txt              # Remove file
```

### Add Another Storage Server (Terminal 5)
```bash
./ss 127.0.0.1 8080 9003
```

New files will be distributed across SS 1 and SS 2 (round-robin).

### Test Concurrent Editing
1. Both alice and bob run: `WRITE hello.txt 0`
2. First one locks the sentence
3. Second gets "Sentence locked" error
4. First completes with `ETIRW`
5. Second can now lock and edit

### Check Logs
```bash
cat nm.log        # Name server operations
cat ss_*.log      # Storage server operations
```

## Quick Command Reference

| Command | Purpose | Example |
|---------|---------|---------|
| `CREATE` | Make new file | `CREATE doc.txt` |
| `READ` | Display content | `READ doc.txt` |
| `WRITE` | Edit file | `WRITE doc.txt 0` |
| `DELETE` | Remove file | `DELETE doc.txt` |
| `VIEW` | List files | `VIEW -al` |
| `INFO` | File details | `INFO doc.txt` |
| `STREAM` | Stream content | `STREAM doc.txt` |
| `LIST` | Show users | `LIST` |
| `ADDACCESS` | Grant access | `ADDACCESS -R doc.txt bob` |
| `REMACCESS` | Remove access | `REMACCESS doc.txt bob` |
| `EXEC` | Run commands | `EXEC script.txt` |
| `UNDO` | Revert changes | `UNDO doc.txt` |
| `exit` | Quit | `exit` |

## Testing Scenarios (5 more minutes)

### Scenario 1: Delimiter Handling
```
> CREATE test.txt
> WRITE test.txt 0
Client: 1 Hello.
Client: 2 How
Client: 3 are
Client: 4 you?
Client: ETIRW
> READ test.txt
```
Expected: `Hello. How are you?` (2 sentences)

### Scenario 2: Mid-Sentence Insertion
```
> WRITE test.txt 0
Client: 2 wonderful
Client: ETIRW
> READ test.txt
```
Expected: `Hello wonderful. How are you?`

### Scenario 3: Access Control Flow
```
# As alice:
> CREATE private.txt
> WRITE private.txt 0
Client: 1 Secret
Client: 2 data.
Client: ETIRW

# As bob (different terminal):
> READ private.txt          # ❌ Denied
> WRITE private.txt 0       # ❌ Denied

# As alice:
> ADDACCESS -R private.txt bob

# As bob:
> READ private.txt          # ✅ Works
> WRITE private.txt 0       # ❌ Still denied (READ only)

# As alice:
> ADDACCESS -W private.txt bob

# As bob:
> WRITE private.txt 0       # ✅ Now works
```

### Scenario 4: Undo
```
> CREATE undo_test.txt
> WRITE undo_test.txt 0
Client: 1 Original
Client: 2 text.
Client: ETIRW

> WRITE undo_test.txt 0
Client: 3 Modified.
Client: ETIRW

> READ undo_test.txt
Original text. Modified.

> UNDO undo_test.txt
> READ undo_test.txt
Original text.
```

## Stopping the System

1. Exit all clients: Type `exit`
2. Stop storage servers: Ctrl+C in SS terminals
3. Stop name server: Ctrl+C in NM terminal

## Cleanup

```bash
# Remove executables and logs
make clean

# Remove storage directories
rm -rf ss_storage_*

# Start fresh
make all
```

## Getting Help

- Type `help` in client for command list
- Check `README.md` for detailed documentation
- See `bugs_and_fixes.md` for troubleshooting
- Review logs in `*.log` files

## Full Example Session

```bash
# Terminal 1
$ ./nm
[NM] Name Server running...

# Terminal 2
$ ./ss 127.0.0.1 8080 9001
[SS] Storage Server running...

# Terminal 3
$ ./client
Enter username: alice
> CREATE story.txt
File created successfully!

> WRITE story.txt 0
Client: 1 Once
Client: 2 upon
Client: 3 a
Client: 4 time.
Client: ETIRW
Write successful!

> WRITE story.txt 1
Client: 1 There
Client: 2 was
Client: 3 a
Client: 4 kingdom.
Client: ETIRW
Write successful!

> READ story.txt
Once upon a time. There was a kingdom.

> STREAM story.txt
Once upon a time. There was a kingdom.

> INFO story.txt
[File details displayed]

> LIST
alice

> exit
Goodbye!
```

---

**You're ready to go! 🚀**

For advanced features and troubleshooting, see `README.md` and `bugs_and_fixes.md`.
