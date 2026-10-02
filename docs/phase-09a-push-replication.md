# Phase 9A — Push Replication

## Overview

Phase 9A introduces **Primary–Backup replication** using TCP.

The primary remains responsible for client requests. For every successful write operation, the primary writes the operation to its own WAL and then pushes the same operation to configured backup nodes.

The architecture is:

```text
                         PRIMARY
                       Port 8080
                           |
                    files/server.wal
                           |
             +-------------+-------------+
             |                           |
             v                           v
       BACKUP 9002                 BACKUP 9003
   backup-9002.wal             backup-9003.wal
```

The goal of this phase is to establish the basic replication path. Failure recovery and replica catch-up are handled in Phase 9B.

---

# 1. Why Replication?

A single primary node creates a single point of failure.

Without replication:

```text
Client
  |
  v
Primary
  |
  v
WAL + Cache
```

If the primary fails, another node cannot immediately reconstruct the latest state unless the data is available somewhere else.

With replication:

```text
                    Primary
                       |
              +--------+--------+
              |                 |
              v                 v
           Backup 1          Backup 2
```

Multiple nodes maintain copies of the primary's operations.

---

# 2. Primary–Backup Model

We use a simple primary–backup architecture.

## Primary

The primary:

- accepts client requests
- owns the main operation sequence
- writes to its WAL
- updates its cache
- pushes writes to backups

Example:

```text
Primary
Port 8080
server.wal
```

## Backup

A backup:

- accepts replication connections
- receives replication operations
- writes them to its own WAL
- updates its local cache
- sends an acknowledgment

Example:

```text
Backup 1
Port 9002
backup-9002.wal
```

and:

```text
Backup 2
Port 9003
backup-9003.wal
```

---

# 3. Push Replication

Phase 9A uses **push replication**.

The primary actively sends each write to its backups.

For example:

```text
Client
   |
   | SET name Vedant
   v
Primary
   |
   | sequence 1
   v
Primary WAL
   |
   +-------------> Backup 9002
   |
   +-------------> Backup 9003
```

The backup acknowledges the operation after successfully writing it to its WAL and updating its cache.

---

# 4. Operation Flow

For a `SET` operation, the primary follows:

```text
1. Receive SET
       |
       v
2. Validate command
       |
       v
3. Write to primary WAL
       |
       v
4. Flush WAL
       |
       v
5. Obtain sequence number
       |
       v
6. Push to backups
       |
       v
7. Update primary cache
       |
       v
8. Return ADDED
```

For `DEL`:

```text
1. Receive DEL
       |
       v
2. Validate command
       |
       v
3. Write DEL to primary WAL
       |
       v
4. Flush WAL
       |
       v
5. Obtain sequence number
       |
       v
6. Push to backups
       |
       v
7. Remove from primary cache
       |
       v
8. Return DELETED
```

---

# 5. Sequence Numbers

Every primary write receives a monotonically increasing sequence number.

Example:

```text
1 SET name Vedant
2 SET branch Electrical
3 SET age 21
4 DEL age
```

The primary owns these sequence numbers.

Backups do **not** generate their own sequence numbers for replicated operations.

Instead, they preserve the primary's sequence number.

Therefore:

```text
Primary:
1 SET name Vedant
2 SET branch Electrical
3 SET age 21
```

Backup:

```text
1 SET name Vedant
2 SET branch Electrical
3 SET age 21
```

This common sequence numbering will later allow us to detect:

- missing operations
- duplicate operations
- out-of-order operations
- replica lag

---

# 6. Replication Protocol

For Phase 9A we use simple line-based TCP messages.

## SET

The primary sends:

```text
REPL_SET 1 name Vedant
```

The backup interprets it as:

```text
sequence = 1
operation = SET
key = name
value = Vedant
```

## DELETE

The primary sends:

```text
REPL_DEL 2 name
```

The backup interprets it as:

```text
sequence = 2
operation = DEL
key = name
```

---

# 7. Backup Processing

When a backup receives:

```text
REPL_SET 1 name Vedant
```

it performs:

```text
Receive operation
       |
       v
Parse sequence
       |
       v
Write to backup WAL
       |
       v
Flush WAL
       |
       v
Update backup cache
       |
       v
ACK
```

The acknowledgment is:

```text
ACK
```

If something fails:

```text
ERROR
```

---

# 8. Backup WAL

Each backup must have its **own WAL file**.

Do not let two backup processes share the same WAL.

Correct:

```text
Primary:
files/server.wal

Backup 9002:
files/backup-9002.wal

Backup 9003:
files/backup-9003.wal
```

Incorrect:

```text
Backup 9002 ─┐
             ├──> files/backup.wal
Backup 9003 ─┘
```

If both backup processes use the same file, every replicated operation will appear twice.

For example:

```text
1 SET name Vedant
1 SET name Vedant
2 SET branch Electrical
2 SET branch Electrical
```

Therefore the backup WAL path should depend on its port.

---

# 9. Primary Replication Manager

The primary uses a `ReplicationManager` to maintain its backup configuration.

Conceptually:

```cpp
struct BackupNode
{
    string host;
    int port;
};
```

The manager stores:

```text
Backup 9002
Backup 9003
```

and sends each operation to every configured backup.

---

# 10. `ReplicationManager` Responsibilities

The manager is responsible for:

### Connecting to a backup

```text
Primary → TCP connect → Backup
```

### Sending a complete message

A `send()` call is not guaranteed to transmit the complete message.

Therefore the implementation uses a `sendAll()` helper.

Conceptually:

```cpp
while (totalSent < message.size())
{
    send(...);
}
```

### Receiving the acknowledgment

The manager waits for:

```text
ACK
```

or reports a failure.

### Closing the connection

Phase 9A uses a simple connection-per-operation model:

```text
connect
send
receive ACK
close
```

This is intentionally simple. Persistent replication connections can be introduced later.

---

# 11. `sendAll()`

TCP is a byte stream.

A call such as:

```cpp
send(socket, message.data(), message.size(), 0);
```

may send fewer bytes than requested.

Therefore the primary uses:

```cpp
bool sendAll(
    int socket,
    const string& message
);
```

The method repeatedly calls `send()` until the entire message is transmitted or an error occurs.

---

# 12. Receiving the ACK

The primary waits for a line ending in:

```text
\n
```

For example:

```text
ACK\n
```

The implementation reads until the newline is found.

This gives the first simple message boundary for replication.

---

# 13. Backup Server

The backup server listens on a configurable port.

Example:

```bash
./backup_server 9002
```

or:

```bash
./backup_server 9003
```

The server:

1. Creates its WAL.
2. Replays the WAL during startup.
3. Creates the TCP listening socket.
4. Accepts replication connections.
5. Processes replication commands.
6. Returns acknowledgments.

---

# 14. Backup Startup

A backup should replay its WAL before accepting new replication operations.

```text
Backup starts
     |
     v
Open backup WAL
     |
     v
Replay WAL
     |
     v
Restore cache
     |
     v
Start TCP server
```

For example:

```text
files/backup-9002.wal
```

is replayed into the 9002 backup's cache.

---

# 15. Primary Code Flow

The primary's `SET` operation follows this pattern:

```cpp
lock_guard<mutex> lock(storageMutex);

if (!wal.logSet(key, value))
    return "ERROR: WAL write failed\n";

wal.flush();

long long sequenceNumber =
    wal.getLastSequenceNumber();

bool replicationSuccessful =
    replicationManager.replicateSet(
        sequenceNumber,
        key,
        value
    );

if (!replicationSuccessful)
{
    cerr << "WARNING: SET replication incomplete"
         << endl;
}

store.put(key, value);

return "ADDED\n";
```

The `DEL` operation follows the same pattern:

```cpp
lock_guard<mutex> lock(storageMutex);

if (!wal.logDelete(key))
    return "ERROR: WAL write failed\n";

wal.flush();

long long sequenceNumber =
    wal.getLastSequenceNumber();

bool replicationSuccessful =
    replicationManager.replicateDelete(
        sequenceNumber,
        key
    );

if (!replicationSuccessful)
{
    cerr << "WARNING: DEL replication incomplete"
         << endl;
}

store.remove(key);

return "DELETED\n";
```

---

# 16. Failure Policy in Phase 9A

Phase 9A uses the following policy:

> A backup failure does not automatically fail the client's write.

Suppose:

```text
Primary = available
Backup 9002 = available
Backup 9003 = unavailable
```

The primary receives:

```text
SET name Vedant
```

It writes:

```text
Primary WAL:
1 SET name Vedant
```

It successfully replicates to 9002.

9003 fails.

The primary logs:

```text
WARNING: SET replication incomplete
```

but the client still receives:

```text
ADDED
```

This means the primary remains available even if a backup is down.

However, this creates replica lag.

That lag must be repaired later.

---

# 17. Why Catch-Up Is Needed

Suppose the primary has:

```text
1 SET name Vedant
2 SET branch Electrical
3 SET age 21
4 SET city Dhanbad
```

Backup 9003 was offline for operations 1–3.

When it comes back, simply sending:

```text
4 SET city Dhanbad
```

is not enough.

The backup needs:

```text
1
2
3
4
```

This is the motivation for **Phase 9B — Replica Catch-Up**.

---

# 18. Example

Start:

### Backup 9002

```bash
./backup_server 9002
```

### Backup 9003

```bash
./backup_server 9003
```

### Primary

```bash
./server
```

Send:

```text
SET name Vedant
SET branch Electrical
SET age 21
```

Primary WAL:

```text
1 SET name Vedant
2 SET branch Electrical
3 SET age 21
```

Backup 9002 WAL:

```text
1 SET name Vedant
2 SET branch Electrical
3 SET age 21
```

Backup 9003 WAL:

```text
1 SET name Vedant
2 SET branch Electrical
3 SET age 21
```

All three nodes are synchronized.

---

# 19. Separate Backup WALs

If the two backups are started independently:

```bash
./backup_server 9002
./backup_server 9003
```

they should create:

```text
files/backup-9002.wal
files/backup-9003.wal
```

Then:

```bash
cat files/backup-9002.wal
```

and:

```bash
cat files/backup-9003.wal
```

should independently show:

```text
1 SET name Vedant
2 SET branch Electrical
3 SET age 21
```

---

# 20. Current Limitations

Phase 9A is intentionally a foundation.

It does not yet handle:

- automatic catch-up
- replica lag tracking
- duplicate replication
- idempotent replication
- missing sequence detection during live replication
- persistent TCP connections
- retry queues
- replica health monitoring
- backup promotion
- automatic failover
- quorum writes
- snapshots
- WAL compaction

These are addressed in later phases.

---

# 21. Important TCP Limitation

The Phase 9A protocol uses newline-delimited messages.

For example:

```text
REPL_SET 1 name Vedant\n
```

TCP itself does not preserve message boundaries.

A single `recv()` can receive:

```text
REPL_SET 1 name Vedant\n
REPL_SET 2 age 21\n
```

or only part of one message.

Therefore the backup implementation maintains a pending buffer and extracts complete lines.

This is a basic framing mechanism.

---

# 22. Important WAL Ordering

The primary performs:

```text
WAL
 ↓
Replication
 ↓
Cache
```

rather than:

```text
Cache
 ↓
WAL
```

This keeps the durable operation recorded before the in-memory state is updated.

The primary's WAL remains the authoritative operation history for replication and recovery.

---

# 23. Sequence Example

After several writes:

```text
Primary:

1 SET name Vedant
2 SET branch Electrical
3 SET age 21
4 SET city Dhanbad
5 DEL age
```

Backup 9002:

```text
1 SET name Vedant
2 SET branch Electrical
3 SET age 21
4 SET city Dhanbad
5 DEL age
```

Backup 9003:

```text
1 SET name Vedant
2 SET branch Electrical
3 SET age 21
4 SET city Dhanbad
5 DEL age
```

All nodes have the same operation sequence.

---

# 24. Phase 9A Success Criteria

Phase 9A is complete when:

- [ ] Primary can configure multiple backups.
- [ ] Primary can push `SET` operations.
- [ ] Primary can push `DEL` operations.
- [ ] Backups preserve the primary's sequence numbers.
- [ ] Backups write replicated operations to their own WAL.
- [ ] Backups update their local cache.
- [ ] Backups return `ACK`.
- [ ] Primary detects replication failures.
- [ ] Each backup uses a separate WAL file.
- [ ] Primary can continue serving writes when a backup is unavailable.
- [ ] Backup restart replays its own WAL.

---

# 25. Final Architecture

At the end of Phase 9A:

```text
                         CLIENT
                            |
                            v
                         PRIMARY
                       Port 8080
                            |
                      server.wal
                            |
                +-----------+-----------+
                |                       |
                v                       v
          BACKUP 9002             BACKUP 9003
          backup-9002.wal         backup-9003.wal
                |                       |
              cache                   cache
```

For a write:

```text
Client
  |
  | SET key value
  v
Primary
  |
  +--> WAL sequence N
  |
  +--> Backup 9002
  |
  +--> Backup 9003
  |
  +--> Primary cache
  |
  v
ADDED
```

The next major improvement is **Phase 9B — Replica Catch-Up**, which solves the case where a backup misses operations and later needs to synchronize with the primary.
