# Phase 7A: Write-Ahead Logging (WAL)

## Objective

Introduce a Write-Ahead Log (WAL) into the distributed key-value store. Until now, values have existed only in memory inside the LRU cache. If the server crashes or restarts, the data is lost.

This phase builds a standalone WAL component before integrating it with the TCP server.

## What We Will Implement

- Open a log file in append mode.
- Record `SET key value` operations.
- Record `DEL key` operations.
- Protect concurrent writes using a mutex.
- Flush buffered output.
- Test the WAL independently.
- Inspect the generated log manually.

## Why WAL Is Required

Current flow:

```text
Client -> TCP Server -> LRU Cache
```

The cache is volatile. With WAL, every modification is recorded in a file:

```text
Client -> TCP Server -> WAL -> LRU Cache
                         |
                         v
                  Recovery history
```

After a restart, the server can replay the WAL records in order and rebuild its state.

## Initial Record Format

This phase uses a human-readable text format:

```text
SET user:101:name Vedant
SET user:101:age 21
DEL user:101:age
```

Each operation occupies one line.

### Benefits

- Easy to read and debug.
- Simple to implement.
- Useful for initial development.

### Current Limitations

- Newline characters inside values are unsupported.
- Keys cannot contain spaces.
- No checksum is included.
- No sequence number is included.
- Corruption detection is not implemented.
- `flush()` does not by itself guarantee physical disk durability; a later version can use `fsync()`.

## Project Structure

```text
distributed-kv-store/
├── include/
│   ├── lru_cache.h
│   └── wal.h
├── src/
│   ├── lru_cache.cpp
│   ├── tcp_server.cpp
│   ├── tcp_client.cpp
│   └── wal.cpp
└── tests/
    └── wal_test.cpp
```

## Implementation Plan

1. Create `WAL` as a separate class.
2. Open the file using append mode so old records are not overwritten.
3. Lock the stream during each write.
4. Return `true` or `false` depending on whether the stream accepts the record.
5. Flush the file during testing.
6. Integrate WAL with `SET` and `DEL` only after standalone testing succeeds.

## Testing Checklist

- [ ] Create `include/wal.h`.
- [ ] Create `src/wal.cpp`.
- [ ] Create `tests/wal_test.cpp`.
- [ ] Compile with warnings enabled.
- [ ] Run the test executable.
- [ ] Inspect the generated `test.wal` file.
- [ ] Verify both `SET` and `DEL` records.
- [ ] Delete the test file and repeat the test to confirm clean output.

## Compile and Run

```bash
g++ -std=c++17 -Wall -Wextra -pedantic -pthread \
    src/wal.cpp tests/wal_test.cpp -Iinclude -o wal_test
```

Run:

```bash
./wal_test
```

Inspect the log:

```bash
cat test.wal
```

## Expected Log

```text
SET user:101:name Vedant
SET user:101:description Electrical Engineering Student
SET user:101:age 21
DEL user:101:age
```

Because the file is opened in append mode, running the test repeatedly adds more records. For a clean test run:

```bash
rm -f test.wal
./wal_test
cat test.wal
```

## Recovery Example

Given these records:

```text
SET user:101:name Vedant
SET user:101:age 21
DEL user:101:age
```

Replay them in order:

1. Store `user:101:name = Vedant`.
2. Store `user:101:age = 21`.
3. Delete `user:101:age`.

Final recovered state:

```text
user:101:name = Vedant
```

The age key is absent because the last operation deleted it.

## Concurrency Notes

The TCP server creates one thread per client. Multiple threads may attempt to append to the WAL at the same time. The `WAL` class therefore uses a mutex around stream operations.

The mutex protects concurrent access inside one process. It does not coordinate writes between separate processes or machines.

## Next Phase: 7B — WAL Replay

The next phase will:

1. Read the WAL file line by line.
2. Parse `SET` and `DEL` records.
3. Apply operations in their original order.
4. Rebuild the LRU cache.
5. Handle malformed records.
6. Test recovery after a simulated restart.
