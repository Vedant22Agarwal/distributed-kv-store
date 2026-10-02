# Phase 12 — Robust Backup Failure & Recovery

## 1. Overview

Phase 11 established health monitoring for the distributed key-value store.

The primary now periodically checks its backups and tracks their state:

```text
HEALTHY
   |
   | backup becomes unreachable
   v
DOWN
   |
   | backup becomes reachable
   v
STARTING
   |
   | primary detects missing sequences
   v
LAGGING
   |
   | WAL catch-up completes
   v
HEALTHY
```

Phase 12 focuses on making backup failure and recovery behavior more robust.

The goal is to strengthen the existing replication mechanisms so that repeated failures, reconnects, stale replicas, and recovery are handled predictably.

---

# 2. Current Architecture

```text
                    PRIMARY
                     :8080
                       |
              +--------+--------+
              |                 |
              v                 v
        BACKUP 9002       BACKUP 9003
```

The primary maintains:

- Primary WAL
- LRU cache
- Replica metadata
- Replica state
- Last known sequence number
- Health-monitoring thread

Each backup maintains:

- Its own WAL
- Its own LRU cache
- Replicated key-value data
- Last applied sequence number

Current WAL files:

```text
files/server.wal
files/backup-9002.wal
files/backup-9003.wal
```

---

# 3. Why Phase 12 Is Needed

The system can already recover a backup:

```text
9002 HEALTHY
     |
     | process killed
     v
9002 DOWN
     |
     | process restarted
     v
9002 STARTING
     |
     v
9002 LAGGING
     |
     | WAL catch-up
     v
9002 HEALTHY
```

However, a production-style system must also consider:

1. A backup repeatedly crashes.
2. A backup is restarted but has stale data.
3. A backup becomes reachable but is still missing WAL records.
4. A backup disappears while writes are happening.
5. A backup comes back while new writes are still arriving.
6. A backup becomes unavailable during replication.
7. Connection attempts repeatedly fail.
8. A backup reports an incorrect sequence.
9. A recovered backup must not serve stale data.
10. Multiple failures and recoveries happen during the same server lifetime.

---

# 4. Phase 12 Goals

```text
1. Clean failure detection
2. Controlled reconnect attempts
3. Correct state transitions
4. Correct sequence tracking
5. Safe recovery
6. No stale replica reads
7. Correct WAL catch-up
8. Repeated failure/recovery support
9. Clear logging
10. Failure testing
```

---

# 5. Replica State Model

The current state machine remains:

```text
STARTING
   |
   v
HEALTHY <------+
   |           |
   | failure   | recovery
   v           |
 DOWN ---------+
   |
   | reconnect
   v
STARTING
   |
   | missing sequence
   v
LAGGING
   |
   | catch-up successful
   v
HEALTHY
```

### STARTING

The backup has become reachable again, but the primary has not yet established that it is synchronized.

A STARTING replica should not be used for normal reads.

### HEALTHY

The backup is reachable and synchronized with the primary.

A HEALTHY replica can be used for reads.

### LAGGING

The backup is reachable but is behind the primary.

The primary should attempt WAL catch-up.

A LAGGING replica should not be used for reads.

### DOWN

The backup is currently unreachable.

It should not be used for reads.

Writes may continue through the remaining replicas and primary.

---

# 6. Important Safety Rule

A recovered backup must never immediately become a read source merely because its TCP connection succeeds.

Incorrect:

```text
connect succeeds
     |
     v
HEALTHY
     |
     v
serve GET
```

Correct:

```text
connect succeeds
     |
     v
STARTING
     |
     v
check sequence
     |
     +----------+
     |          |
   equal      behind
     |          |
     v          v
 HEALTHY     LAGGING
                |
                v
            catch-up
                |
                v
             HEALTHY
```

This prevents stale reads.

---

# 7. Sequence Number Invariant

The primary sequence number is the source of truth for replication progress.

If:

```text
backupSequence == primarySequence
```

the backup is synchronized.

If:

```text
backupSequence < primarySequence
```

the backup is LAGGING.

If the backup reports:

```text
backupSequence > primarySequence
```

the condition is invalid and should not be treated as healthy.

---

# 8. Failure During a Write

Suppose:

```text
Primary sequence = 20
Backup 9002 = 19
Backup 9003 = 20
```

A client sends:

```text
SET apple 100
```

The primary creates:

```text
sequence = 21
```

Possible result:

```text
9002 -> failure
9003 -> success
```

The current project policy allows the client write to succeed.

The resulting state is approximately:

```text
Primary = 21
9003   = 21 HEALTHY
9002   = DOWN or LAGGING
```

The failed backup is repaired later using WAL records.

---

# 9. Failure While Multiple Writes Occur

Consider:

```text
9002 DOWN
```

Then:

```text
SET A 10
SET B 20
SET C 30
SET D 40
```

The primary WAL preserves the missing records.

For example:

```text
21 SET A 10
22 SET B 20
23 SET C 30
24 SET D 40
```

When 9002 returns:

```text
9002 sequence = 20
Primary sequence = 24
```

The primary requests records:

```text
21
22
23
24
```

and sends them sequentially.

The backup validates each sequence.

---

# 10. Repeated Failure

A backup may repeatedly fail:

```text
HEALTHY
   ↓
DOWN
   ↓
STARTING
   ↓
LAGGING
   ↓
HEALTHY
   ↓
DOWN
   ↓
STARTING
   ↓
...
```

The implementation must not assume recovery happens only once.

Every recovery cycle should:

1. Detect the backup.
2. Move it to STARTING.
3. Check sequence.
4. Move it to HEALTHY or LAGGING.
5. Catch up if required.
6. Only then allow reads.

---

# 11. Read Safety

Current read routing is:

```text
GET
 |
 +----> healthy replica?
 |          |
 |          +---- YES -> read replica
 |
 +----> NO
            |
            v
         primary
```

Phase 12 strengthens the rule:

```text
Only HEALTHY replicas may serve reads.
```

The following must never serve normal reads:

```text
STARTING
LAGGING
DOWN
```

This is particularly important immediately after recovery.

---

# 12. Reconnection Behavior

When a DOWN backup becomes reachable:

```text
DOWN
 |
 | successful TCP connection
 v
STARTING
```

The primary then performs a status check.

It must not directly set:

```text
STARTING -> HEALTHY
```

unless sequence numbers prove synchronization.

---

# 13. Catch-Up Safety

Catch-up should be sequential.

Suppose:

```text
Primary = 100
Backup = 95
```

Missing records are:

```text
96
97
98
99
100
```

The primary sends:

```text
96
97
98
99
100
```

The backup validates:

```text
expected = currentBackupSequence + 1
```

Therefore:

```text
95 -> 96
96 -> 97
97 -> 98
98 -> 99
99 -> 100
```

Any unexpected sequence should be rejected.

---

# 14. What Happens If Catch-Up Fails?

Suppose:

```text
Backup = 95
Primary = 100
```

The primary starts catch-up.

Records:

```text
96
97
98
99
100
```

Suppose sending record 98 fails.

The backup should not be considered healthy.

The primary should leave it in a non-healthy state and retry during a later health-monitor cycle.

Conceptually:

```text
LAGGING
   |
   | catch-up fails
   v
LAGGING
```

or:

```text
LAGGING
   |
   | connection failure
   v
DOWN
```

depending on the failure type.

---

# 15. Avoiding Stale Reads

Imagine:

```text
Primary = sequence 30
Backup = sequence 25
```

The backup reconnects.

It must not immediately serve:

```text
GET apple
```

because its data may reflect only sequence 25.

Instead:

```text
STARTING
   |
   v
sequence check
   |
   v
LAGGING
   |
   v
catch-up
   |
   v
sequence 30
   |
   v
HEALTHY
   |
   v
allowed to serve GET
```

---

# 16. Primary Failure vs Backup Failure

Phase 12 concerns backup failure.

The primary remains the authoritative node.

If a backup fails:

```text
Primary remains active.
```

If all backups fail:

```text
Primary continues serving according to
the current project policy.
```

Automatic primary election or consensus is outside the scope of this phase.

---

# 17. Logging Requirements

Useful logs should clearly describe:

### Failure

```text
Backup 127.0.0.1:9002 is DOWN
```

### Recovery

```text
Backup 127.0.0.1:9002 state changed to STARTING
```

### Lag detection

```text
Backup 127.0.0.1:9002 state changed to LAGGING
```

### Catch-up

```text
Starting catch-up for backup 127.0.0.1:9002
```

### Recovery completion

```text
Backup 127.0.0.1:9002 catch-up completed
Backup 127.0.0.1:9002 state changed to HEALTHY
```

Repeated health checks should not produce repeated state-change logs when the state has not changed.

---

# 18. Phase 12 Implementation Plan

## 12.1 — Clean failure handling

Review all failure paths:

```text
connect()
send()
receive()
STATUS
REPL_SET
REPL_DEL
catch-up
GET
```

Every failure must result in a consistent replica state.

## 12.2 — Controlled reconnect

Ensure DOWN replicas are periodically retried without causing unnecessary connection churn.

The health monitor remains responsible for reconnection.

## 12.3 — Recovery validation

A newly reachable backup must go through:

```text
DOWN
  ↓
STARTING
  ↓
STATUS
  ↓
HEALTHY / LAGGING
```

## 12.4 — Catch-up validation

Verify:

```text
backupSequence
        ↓
primarySequence
        ↓
missing WAL records
        ↓
sequential replication
        ↓
final STATUS
```

## 12.5 — Read safety

Verify that:

```text
STARTING
LAGGING
DOWN
```

replicas cannot serve normal GET requests through the primary's healthy-replica routing.

## 12.6 — Repeated failure testing

Test:

```text
kill
restart
catch-up
kill again
restart again
catch-up again
```

## 12.7 — Concurrent-write recovery testing

While a backup is DOWN:

```text
SET A
SET B
SET C
SET D
...
```

Then restart the backup and verify that all missing WAL records are replayed.

---

# 19. Failure Test Matrix

| Test | Expected Result |
|---|---|
| Kill backup | State becomes DOWN |
| Restart backup | State becomes STARTING |
| Backup behind | State becomes LAGGING |
| Catch-up succeeds | State becomes HEALTHY |
| GET while DOWN | Primary fallback |
| GET while LAGGING | Primary fallback |
| GET while STARTING | Primary fallback |
| Multiple writes while DOWN | WAL preserves all writes |
| Restart after many writes | Backup catches up |
| Kill twice | Recovery works repeatedly |
| Catch-up connection failure | Backup remains non-HEALTHY |
| Both backups down | Primary continues according to current policy |

---

# 20. Completion Criteria

```text
[ ] Backup failures are detected cleanly

[ ] DOWN backups are retried

[ ] Recovered backups enter STARTING

[ ] STARTING backups are sequence-checked

[ ] LAGGING backups automatically catch up

[ ] Only HEALTHY backups serve replica reads

[ ] Failed catch-up does not mark a backup HEALTHY

[ ] Multiple failure/recovery cycles work

[ ] Writes during backup downtime are recovered

[ ] WAL records are applied in order

[ ] Logs clearly describe recovery

[ ] No stale replica reads occur
```

---

# 21. Current System After Phase 11

At the end of Phase 11:

```text
                    PRIMARY
                     :8080
                       |
             Health Monitor
                       |
          +------------+------------+
          |                         |
          v                         v
     BACKUP 9002               BACKUP 9003
          |                         |
          v                         v
      WAL + Cache               WAL + Cache
```

The primary now has:

```text
WAL
LRU Cache
TCP server
Sequence numbers
Replication
Replica catch-up
Replica state machine
Health monitoring
Thread-safe health checks
Automatic recovery
```

---

# 22. Phase 12 Target Architecture

```text
                         PRIMARY
                          :8080
                            |
             +--------------+--------------+
             |                             |
       Replication                   Health Monitor
             |                             |
      +------+-------+                     |
      |              |                     |
      v              v                     v
  Backup 9002    Backup 9003       Failure Detection
      |              |                     |
      v              v                     v
  WAL + Cache    WAL + Cache       Recovery Validation
      |              |                     |
      +--------------+---------------------+
                     |
                  Catch-up
                     |
                  HEALTHY
```

The core invariant remains:

```text
Only synchronized replicas can serve replica reads.
```

---

# 23. Important Scope Boundary

Phase 12 does NOT introduce:

- Leader election
- Consensus
- Raft
- Automatic primary promotion
- Multi-primary writes
- Cross-primary conflict resolution

Those are separate distributed-systems problems.

The goal here is to make the existing primary-backup architecture reliable under repeated backup failure and recovery.

---

# 24. Next Implementation Step

We will start with:

## Phase 12.1 — Clean Failure Handling

We will inspect:

```text
connectToBackup()
getBackupStatus()
replicateSet()
replicateDelete()
catchUpBackup()
getFromHealthyReplica()
```

and make sure every failure path produces the correct state:

```text
Connection failure     -> DOWN
Status failure         -> DOWN
Replication failure    -> DOWN
Catch-up failure       -> LAGGING/DOWN
Successful reconnect   -> STARTING
Synchronized replica   -> HEALTHY
```

We will implement this one step at a time and test after each change.
