# Phase 7B: WAL Replay — Crash Recovery

## 1. Objective

Phase 7A introduced a Write-Ahead Log (WAL) that records `SET` and `DEL` operations.

However, writing records is only useful if the server can read them after a restart.

In this phase, we implement WAL replay. Replay reads the log from beginning to end and applies each operation to an in-memory `LRUCache`.

The replay process allows the server to reconstruct its state after:

- A normal restart.
- A process crash.
- A machine reboot.
- Temporary loss of in-memory state.

This phase focuses on learning and correctness. Production-grade durability, checksums, sequence numbers, snapshots, and corruption recovery will be added later.

---

## 2. Current Problem

At the moment, the key-value store is held in memory:

```text
Client
   |
   v
TCP Server
   |
   v
LRU Cache
```

When the process exits, the cache is destroyed.

Example:

```text
Before restart:

user:101:name = Vedant
user:101:age  = 21
```

After restart:

```text
Cache is empty
```

The WAL contains the history needed to reconstruct the state:

```text
SET user:101:name Vedant
SET user:101:age 21
DEL user:101:age
```

The server can replay these records and recover the final state:

```text
user:101:name = Vedant
```

---

## 3. Replay Principle

Replay must preserve the order of operations.

Consider:

```text
SET balance 100
SET balance 500
DEL balance
SET balance 200
```

The final value must be:

```text
balance = 200
```

The records cannot be applied in an arbitrary order.

The replay algorithm is:

```text
Read the next record
       |
       v
Parse the operation
       |
       v
If SET: insert/update key
       |
       v
If DEL: remove key
       |
       v
Read the next record
```

Replay continues until the end of the file.

---

## 4. Design Choice

The WAL class will expose a method that receives an `LRUCache` reference:

```cpp
bool replay(LRUCache& store);
```

This method:

1. Opens the WAL file for reading.
2. Reads records line by line.
3. Parses each record.
4. Applies the operation to the provided cache.
5. Returns whether replay completed successfully.

The cache is passed by reference because replay must modify the existing cache object.

---

## 5. Updated Project Structure

```text
distributed-kv-store/
├── include/
│   ├── lru_cache.h
│   └── wal.h
├── src/
│   ├── wal.cpp
│   ├── tcp_server.cpp
│   └── tcp_client.cpp
├── tests/
│   └── wal_replay_test.cpp
└── test.wal
```

---

## 6. Update `include/wal.h`

Replace the contents of `include/wal.h` with:

```cpp
#ifndef WAL_H
#define WAL_H

#include <fstream>
#include <mutex>
#include <string>

#include "lru_cache.h"

using namespace std;

/*
 * ============================================================
 * WRITE-AHEAD LOG
 * ============================================================
 *
 * Supported records:
 *
 *     SET key value
 *     DEL key
 *
 * Responsibilities:
 *
 *     1. Append SET records.
 *     2. Append DEL records.
 *     3. Flush output.
 *     4. Replay records into an LRUCache.
 *
 */

class WAL
{
private:
    /*
     * Output stream used when appending new records.
     */

    ofstream logFile;

    /*
     * Protects writes and flush operations within this process.
     */

    mutex walMutex;

    /*
     * Location of the WAL file.
     */

    string filePath;

public:
    /*
     * Open the WAL in append mode.
     */

    explicit WAL(const string& path);

    /*
     * Close the WAL file.
     */

    ~WAL();

    /*
     * Prevent copying because the class owns a file stream
     * and a mutex.
     */

    WAL(const WAL&) = delete;
    WAL& operator=(const WAL&) = delete;

    /*
     * Append a SET record.
     *
     * Format:
     *
     *     SET key value
     */

    bool logSet(
        const string& key,
        const string& value
    );

    /*
     * Append a DEL record.
     *
     * Format:
     *
     *     DEL key
     */

    bool logDelete(const string& key);

    /*
     * Flush buffered output.
     */

    void flush();

    /*
     * Read the WAL from the beginning and apply all valid
     * operations to the supplied cache.
     *
     * Returns true when the file can be read and replayed.
     */

    bool replay(LRUCache& store);
};

#endif
```

---

## 7. Update `src/wal.cpp`

Replace the contents of `src/wal.cpp` with:

```cpp
/*
 * ============================================================
 * REPLAY WAL
 * ============================================================
 */

bool WAL::replay(LRUCache& store)
{
    /*
     * Open a separate input stream.
     *
     * We do not use logFile for reading because logFile is an
     * output stream used for appending new records.
     */

    ifstream inputFile(filePath);

    if (!inputFile.is_open())
    {
        cerr << "Failed to open WAL for replay: "
             << filePath
             << endl;

        return false;
    }

    string line;
    size_t lineNumber = 0;

    /*
     * Read the WAL one line at a time.
     */

    while (getline(inputFile, line))
    {
        lineNumber++;

        /*
         * Ignore empty lines.
         */

        if (line.empty())
        {
            continue;
        }

        stringstream input(line);

        string operation;
        input >> operation;

        /*
         * ====================================================
         * SET RECORD
         * ====================================================
         *
         * Expected format:
         *
         *     SET key value
         *
         * Values may contain spaces.
         */

        if (operation == "SET")
        {
            string key;

            input >> key;

            if (key.empty())
            {
                cerr << "Invalid SET record at line "
                     << lineNumber
                     << endl;

                continue;
            }

            /*
             * Read the remaining part of the line as the value.
             */

            string value;

            getline(input, value);

            /*
             * Remove the separator space after the key.
             */

            if (!value.empty() && value[0] == ' ')
            {
                value.erase(0, 1);
            }

            /*
             * Empty values are not supported by this initial
             * text-based format.
             */

            if (value.empty())
            {
                cerr << "Invalid SET value at line "
                     << lineNumber
                     << endl;

                continue;
            }

            /*
             * Reconstruct the cache state.
             */

            store.put(key, value);
        }

        /*
         * ====================================================
         * DEL RECORD
         * ====================================================
         *
         * Expected format:
         *
         *     DEL key
         */

        else if (operation == "DEL")
        {
            string key;

            input >> key;

            if (key.empty())
            {
                cerr << "Invalid DEL record at line "
                     << lineNumber
                     << endl;

                continue;
            }

            /*
             * Remove the key from the cache.
             */

            store.remove(key);
        }

        /*
         * ====================================================
         * UNKNOWN RECORD
         * ====================================================
         */

        else
        {
            cerr << "Unknown WAL operation at line "
                 << lineNumber
                 << ": "
                 << operation
                 << endl;

            /*
             * Continue replaying subsequent records instead
             * of stopping the entire recovery process.
             */
        }
    }

    /*
     * Check whether the input stream failed for a reason other
     * than reaching the end of the file.
     */

    if (inputFile.bad())
    {
        cerr << "Error while reading WAL file"
             << endl;

        return false;
    }

    inputFile.close();

    return true;
}
```

---

## 8. Create `tests/wal_replay_test.cpp`

```cpp
#include "../include/lru_cache.h"
#include "../include/wal.h"

#include <iostream>

using namespace std;

/*
 * ============================================================
 * WAL REPLAY TEST
 * ============================================================
 *
 * This test performs two stages:
 *
 *     Stage 1:
 *         Write operations to the WAL.
 *
 *     Stage 2:
 *         Create a new empty cache and replay the WAL into it.
 *
 * This simulates the recovery process after a restart.
 *
 */

int main()
{
    const string filePath = "replay_test.wal";

    /*
     * --------------------------------------------------------
     * STAGE 1: CREATE WAL RECORDS
     * --------------------------------------------------------
     */

    {
        WAL wal(filePath);

        /*
         * Add a user name.
         */

        if (!wal.logSet("user:101:name", "Vedant"))
        {
            cerr << "Failed to write name record\n";
            return 1;
        }

        /*
         * Add a user age.
         */

        if (!wal.logSet("user:101:age", "21"))
        {
            cerr << "Failed to write age record\n";
            return 1;
        }

        /*
         * Add a value containing spaces.
         */

        if (!wal.logSet(
                "user:101:description",
                "Electrical Engineering Student"
            ))
        {
            cerr << "Failed to write description record\n";
            return 1;
        }

        /*
         * Delete the age key.
         */

        if (!wal.logDelete("user:101:age"))
        {
            cerr << "Failed to write delete record\n";
            return 1;
        }

        wal.flush();
    }

    cout << "WAL records created\n";

    /*
     * --------------------------------------------------------
     * STAGE 2: SIMULATE A RESTART
     * --------------------------------------------------------
     *
     * This is a new cache object.
     *
     * It starts empty and has no knowledge of the previous
     * cache state.
     */

    LRUCache recoveredStore(100);

    /*
     * Replay the WAL into the new cache.
     */

    WAL recoveryWAL(filePath);

    if (!recoveryWAL.replay(recoveredStore))
    {
        cerr << "WAL replay failed\n";
        return 1;
    }

    cout << "WAL replay completed\n";

    /*
     * --------------------------------------------------------
     * VERIFY RECOVERED STATE
     * --------------------------------------------------------
     */

    auto name = recoveredStore.get("user:101:name");

    if (name.has_value() && name.value() == "Vedant")
    {
        cout << "Name recovered successfully\n";
    }
    else
    {
        cerr << "Name recovery failed\n";
        return 1;
    }

    /*
     * The age key was deleted in the WAL, so it should not
     * exist after replay.
     */

    auto age = recoveredStore.get("user:101:age");

    if (!age.has_value())
    {
        cout << "Deleted age key is absent\n";
    }
    else
    {
        cerr << "Deleted age key was incorrectly recovered\n";
        return 1;
    }

    /*
     * Verify that values containing spaces were recovered.
     */

    auto description = recoveredStore.get(
        "user:101:description"
    );

    if (
        description.has_value() &&
        description.value() == "Electrical Engineering Student"
    )
    {
        cout << "Description recovered successfully\n";
    }
    else
    {
        cerr << "Description recovery failed\n";
        return 1;
    }

    cout << "WAL replay test passed\n";

    return 0;
}
```

---

## 9. Compile the Replay Test

Before running the test, remove the previous test file so that records do not accumulate across runs:

```bash
rm -f replay_test.wal
```

Compile:

```bash
g++ -std=c++17 -Wall -Wextra -pedantic \
    -pthread \
    src/lru_cache.cpp \
    src/wal.cpp \
    tests/7b_wal_replay_test.cpp \
    -Iinclude \
    -o wal_replay_test
```

Run:

```bash
./wal_replay_test
```

Expected output:

```text
WAL records created
WAL replay completed
Name recovered successfully
Deleted age key is absent
Description recovered successfully
WAL replay test passed
```

The exact output depends on whether all operations and validations succeed.

---

## 10. Inspect the WAL

Run:

```bash
cat replay_test.wal
```

Expected:

```text
SET user:101:name Vedant
SET user:101:age 21
SET user:101:description Electrical Engineering Student
DEL user:101:age
```

The cache after replay should contain:

```text
user:101:name = Vedant
user:101:description = Electrical Engineering Student
```

The key `user:101:age` should be absent.

---

## 11. Recovery Walkthrough

The WAL records are processed in order.

### Record 1

```text
SET user:101:name Vedant
```

Cache:

```text
user:101:name = Vedant
```

### Record 2

```text
SET user:101:age 21
```

Cache:

```text
user:101:name = Vedant
user:101:age = 21
```

### Record 3

```text
SET user:101:description Electrical Engineering Student
```

Cache:

```text
user:101:name = Vedant
user:101:age = 21
user:101:description = Electrical Engineering Student
```

### Record 4

```text
DEL user:101:age
```

Final cache:

```text
user:101:name = Vedant
user:101:description = Electrical Engineering Student
```

Replay is therefore an ordered state reconstruction process.

---

## 12. Why Do We Use a Separate `ifstream`?

The WAL already has:

```cpp
ofstream logFile;
```

This stream is used for appending records.

For replay, we use:

```cpp
ifstream inputFile(filePath);
```

The two streams have different responsibilities:

| Stream | Purpose |
|---|---|
| `ofstream` | Append new WAL records |
| `ifstream` | Read existing WAL records |

Using a separate input stream makes the code easier to understand and avoids mixing writing and reading state in the same stream.

---

## 13. Why Is the Cache Passed by Reference?

The replay method is:

```cpp
bool replay(LRUCache& store);
```

The `&` means that the method receives a reference to the original cache object.

Without a reference:

```cpp
bool replay(LRUCache store);
```

the cache could be copied, and changes might be made to a temporary copy rather than the original cache.

With a reference:

```cpp
LRUCache recoveredStore(100);

wal.replay(recoveredStore);
```

the method modifies `recoveredStore` directly.

---

## 14. Invalid Records

The current implementation reports invalid or unknown operations:

```text
SET
UNKNOWN key value
DEL
```

It continues processing subsequent records.

This behavior is useful for early learning and debugging, but it is not yet a complete crash-recovery policy.

A production system must decide whether to:

- Stop recovery immediately.
- Ignore only a damaged final record.
- Quarantine the corrupted WAL.
- Recover from the last valid checkpoint.
- Use checksums to detect partial writes.
- Alert an operator.

We will improve this in later phases.

---

## 15. Important Limitations

### 15.1 No checksum

A corrupted record may not be detected reliably.

### 15.2 No sequence numbers

We cannot yet identify missing, duplicated, or out-of-order operations.

### 15.3 No durable flush

`flush()` does not guarantee that the data has reached physical storage.

### 15.4 No snapshots

Every restart requires replaying the complete WAL.

Large logs can make startup slow.

### 15.5 Text format restrictions

Newline characters in keys or values are not supported.

### 15.6 Cache capacity

The recovered cache has a fixed capacity. If the WAL contains more unique keys than the cache can hold, older entries may be evicted according to the LRU policy.

This is one reason a future persistent database or complete state store may be required beyond the cache.

---

## 16. Testing Checklist

- [x] Updated `include/wal.h`.
- [x] Updated `src/wal.cpp`.
- [x] Created `tests/wal_replay_test.cpp`.
- [x] Compiled with C++17.
- [x] Created WAL records.
- [x] Created a fresh empty cache.
- [x] Replayed the WAL.
- [x] Verified a recovered SET key.
- [x] Verified a deleted key is absent.
- [x] Verified values containing spaces.
- [x] Inspected the WAL manually.

---

## 17. Next Phase

The next improvement will be to integrate WAL with the TCP server.

The intended write flow will become:

```text
Client sends SET
       |
       v
Server validates command
       |
       v
Append operation to WAL
       |
       v
Flush WAL
       |
       v
Update LRU cache
       |
       v
Return success
```

Before integration, we should eventually add stronger error handling and define the durability guarantees clearly.

After integration, we can simulate:

```text
1. Start server
2. SET some keys
3. Stop server
4. Start server again
5. Replay WAL
6. GET the previous keys
```

That will demonstrate actual restart recovery.
