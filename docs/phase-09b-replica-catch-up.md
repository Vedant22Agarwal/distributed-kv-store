# Phase 9B — Replica Catch-Up

## Overview

Phase 9A implemented push replication: the primary writes an operation to its WAL and pushes it to configured backups.

The remaining problem is a backup that was offline or joined after the primary had already processed operations.

Example:

```text
Primary WAL:
1 SET name Vedant
2 SET branch Electrical
3 SET age 21
4 SET city Dhanbad
```

A new backup may only receive:

```text
4 SET city Dhanbad
```

It is missing sequences 1–3, so its state is incomplete.

Phase 9B introduces **replica catch-up**: a backup reports its last applied sequence, and the primary sends the missing WAL records in order.

---

## 1. Core Idea

Every primary operation has a monotonically increasing sequence number.

```text
1 → 2 → 3 → 4 → 5 → 6 → ...
```

A replica's progress can therefore be represented by its highest applied sequence.

Example:

```text
Primary:  1 2 3 4 5 6 7 8
Backup:   1 2 3 4
```

The backup is at sequence `4`, so it needs:

```text
5 6 7 8
```

---

## 2. Normal Push Replication

For a new operation:

```text
Client
  |
  v
Primary
  |
  v
WAL sequence 9
  |
  +----> Backup 9002
  |
  +----> Backup 9003
```

This is the mechanism from Phase 9A.

---

## 3. Catch-Up Replication

When a backup is behind:

```text
Backup → Primary

I have sequence 4
```

The primary determines that it has:

```text
5 6 7 8
```

and sends those records to the backup.

```text
Primary
   |
   | 5
   | 6
   | 7
   | 8
   v
Backup
```

After successful application:

```text
Primary = 8
Backup  = 8
```

The backup is synchronized.

---

## 4. Why Sequence Numbers Matter

Sequence numbers let us detect missing operations.

If a backup has:

```text
lastApplied = 4
```

then the next valid operation is:

```text
5
```

If it receives:

```text
6
```

before `5`, there is a gap.

The backup should reject the operation rather than silently creating an inconsistent state.

The basic rule is:

```cpp
receivedSequence == lastAppliedSequence + 1
```

---

## 5. Example: Partially Behind Replica

Primary:

```text
1 SET name Vedant
2 SET branch Electrical
3 SET age 21
4 SET city Dhanbad
5 DEL age
6 SET country India
```

Backup:

```text
1 SET name Vedant
2 SET branch Electrical
```

Progress:

```text
Primary = 6
Backup  = 2
```

Missing range:

```text
3, 4, 5, 6
```

The primary sends:

```text
3 SET age 21
4 SET city Dhanbad
5 DEL age
6 SET country India
```

The backup becomes:

```text
1 SET name Vedant
2 SET branch Electrical
3 SET age 21
4 SET city Dhanbad
5 DEL age
6 SET country India
```

Now both nodes are at sequence `6`.

---

## 6. Example: Completely New Replica

Suppose:

```text
Primary = 100
Backup = 0
```

The backup needs the primary's operations:

```text
1 → 100
```

For the first implementation, we can transfer the missing WAL records sequentially.

Later, this can be optimized with snapshots, checkpoints, batching, or WAL compaction.

---

## 7. Replica Progress

Each configured backup should eventually have information similar to:

```cpp
struct BackupNode
{
    string host;
    int port;
    long long lastSequenceNumber;
};
```

Example:

```text
Backup 9002 → sequence 100
Backup 9003 → sequence 97
```

The primary can determine:

```text
9002 → synchronized
9003 → needs 98, 99, 100
```

---

## 8. Catch-Up Protocol

A simple protocol for Phase 9B can be:

### Backup asks for its progress

```text
LAST_SEQUENCE
```

Primary/backup response:

```text
LAST_SEQUENCE 7
```

### Primary determines the missing range

If:

```text
Primary = 10
Backup = 7
```

then:

```text
8, 9, 10
```

are missing.

### Primary sends records

For example:

```text
CATCHUP 8 SET city Dhanbad
CATCHUP 9 SET country India
CATCHUP 10 DEL age
```

### Backup applies them sequentially

The backup expects:

```text
8
9
10
```

and acknowledges each successful record.

---

## 9. Duplicate Operations

A replica can receive an operation more than once.

For example:

```text
lastApplied = 4
received = 4
```

The operation may already have been applied.

Later we will explicitly distinguish:

```text
sequence == expected
```

from:

```text
sequence <= lastApplied
```

This is the beginning of **idempotent replication**.

---

## 10. Important Phase 9A Limitation

Phase 9A currently supports direct push replication:

```text
REPL_SET
REPL_DEL
```

That is sufficient when the backup is already synchronized.

It does not yet provide a way for a new or stale backup to request missing records.

That is the exact problem Phase 9B solves.

---

## 11. Catch-Up Flow

The desired flow is:

```text
                 PRIMARY
                    |
             Primary sequence
                    |
                    v
             Check replica
                    |
          +---------+---------+
          |                   |
       Up to date          Behind
          |                   |
          v                   v
        READY              CATCH UP
                              |
                              v
                     Send missing WAL
                              |
                              v
                       Validate order
                              |
                              v
                        Apply records
                              |
                              v
                       Update progress
                              |
                              v
                            READY
```

---

## 12. What We Are Not Implementing Yet

Phase 9B is intentionally limited.

We are not yet implementing:

- leader election
- automatic failover
- quorum writes
- Raft
- replica promotion
- health monitoring
- snapshots
- WAL compaction
- network partition handling
- full replica state machine

Those will come later.

---

## 13. Implementation Plan

### Phase 9B.1 — Replica Progress Tracking

Add a last-applied sequence to each replica and provide a way to query it.

### Phase 9B.2 — WAL Range Reading

Add a method that can retrieve records between two sequence numbers.

Example:

```cpp
getRecordsFromSequence(8, 10)
```

### Phase 9B.3 — Catch-Up Transfer

Send the missing records to the backup in sequence order.

### Phase 9B.4 — Backup Validation

The backup accepts only the expected next sequence.

### Phase 9B.5 — Catch-Up Completion

When:

```text
backupSequence == primarySequence
```

the backup is synchronized.

### Phase 9B.6 — Resume Normal Push Replication

After catch-up, new operations continue through the normal Phase 9A push path.

---

## 14. Target Architecture

```text
                         PRIMARY
                       Port 8080
                           |
                    server.wal
                           |
             +-------------+-------------+
             |                           |
             v                           v
       BACKUP 9002                 BACKUP 9003
       sequence = 10               sequence = 7
                                         |
                                         |
                                  Catch-up needed
                                         |
                                         v
                                   sequences 8–10
                                         |
                                         v
                                   sequence = 10
```

After catch-up:

```text
Primary  = 10
Backup 1 = 10
Backup 2 = 10
```

Then normal push replication continues:

```text
11 → backups
12 → backups
13 → backups
```

---

## 15. Success Criteria

Phase 9B is complete when we can demonstrate:

- [ ] A backup can start with an older WAL.
- [ ] The primary can determine the backup's last sequence.
- [ ] The primary identifies missing records.
- [ ] Missing records are transferred in order.
- [ ] The backup rejects sequence gaps.
- [ ] The backup reaches the primary's latest sequence.
- [ ] Normal push replication resumes afterward.
- [ ] Restarting a caught-up backup preserves its progress.
- [ ] A completely new backup can catch up from sequence `0`.

---

## 16. Key Mental Model

The central idea is:

```text
WAL sequence
     ↓
replica progress
     ↓
missing range
     ↓
catch-up
     ↓
synchronized replica
```

Example:

```text
Primary = 25
Backup  = 20

Missing:
21, 22, 23, 24, 25

       ↓

Catch-up

       ↓

Backup = 25
```

Only after that should normal push replication continue.

---

## Next Step

The first implementation step is **Phase 9B.1 — Replica Progress Tracking**.

We will:

1. Add `lastSequenceNumber` to `BackupNode`.
2. Add a `STATUS`/`LAST_SEQUENCE` protocol.
3. Let the primary discover how far each backup has progressed.
4. Use that information to decide whether catch-up is required.
