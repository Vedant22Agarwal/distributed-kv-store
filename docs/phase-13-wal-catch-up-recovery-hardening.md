# Phase 13 — WAL-Based Backup Catch-Up & Recovery Hardening

## 1. Overview

Phase 13 hardens the backup recovery mechanism built in the earlier replication phases.

The current system already supports:

- Primary WAL persistence
- Per-backup WAL files
- Sequence numbers
- Strict sequence validation on backups
- Replica states: `STARTING`, `HEALTHY`, `LAGGING`, `DOWN`
- Automatic detection of lagging backups
- WAL-based catch-up
- Automatic recovery after backup failure

The goal of Phase 13 is **not to redesign replication**. It is to make the existing WAL catch-up mechanism safe when recovery itself is interrupted.

---

## 2. Why Phase 13 Is Needed

Consider:

```text
Primary:
1 2 3 4 5 6 7 8 9 10

Backup:
1 2 3 4 5
```

Catch-up sends:

```text
6 → 7 → 8 → 9 → 10
```

Suppose the backup receives:

```text
6 ACK
7 ACK
8 ACK
```

and then crashes.

After restart it may contain:

```text
1 2 3 4 5 6 7 8
```

while the primary contains:

```text
1 2 3 4 5 6 7 8 9 10
```

The system must ask the backup for its actual sequence and resume from:

```text
9
```

The central principle is:

> **Backup recovery must be based on the backup's actual persisted sequence, not on what the primary assumes it received.**

---

## 3. Goals

Phase 13 should guarantee:

1. Catch-up can be interrupted safely.
2. A backup can restart in the middle of catch-up.
3. The backup reports its actual persisted sequence after restart.
4. Catch-up resumes from the correct missing sequence.
5. Already acknowledged records are not blindly replayed.
6. Duplicate records are rejected by strict sequence validation.
7. Failed catch-up never incorrectly marks a backup `HEALTHY`.
8. A backup can repeatedly fail and recover.
9. The primary WAL remains the source of truth.
10. Recovery eventually converges to the primary sequence.

---

## 4. Existing Architecture

```text
                    PRIMARY
                 WAL sequence N
                      |
          +-----------+-----------+
          |                       |
          v                       v
     Backup 9002             Backup 9003
     own WAL                 own WAL
     sequence X              sequence Y
```

Current WAL files:

```text
files/server.wal
files/backup-9002.wal
files/backup-9003.wal
```

Each backup has its own WAL because different replicas can fail at different times.

---

## 5. Sequence Number Invariant

The fundamental invariant is:

```text
Backup sequence <= Primary sequence
```

Therefore:

```text
Backup == Primary
        ↓
     HEALTHY

Backup < Primary
        ↓
     LAGGING

Backup > Primary
        ↓
      DOWN
```

A backup must never be considered healthy merely because a catch-up operation was attempted.

---

## 6. Current Catch-Up Flow

The current recovery flow is:

```text
Health monitor
      |
      v
Check backup STATUS
      |
      v
Backup is LAGGING
      |
      v
Read missing WAL records
      |
      v
Connect to backup
      |
      v
Send records sequentially
      |
      v
Wait for ACK
      |
      v
Verify final sequence
      |
      v
HEALTHY
```

Example:

```text
Primary = 10
Backup = 6
```

Missing records:

```text
7 8 9 10
```

---

## 7. Important Recovery Rule

The backup's persisted WAL sequence is the source of truth for what it actually has.

For a replication record:

```text
Backup WAL
    ↓
persist record
    ↓
flush
    ↓
update state/cache
    ↓
ACK
```

Only after an ACK can the current catch-up attempt consider that record successfully processed.

However, after any connection failure, the safest action is:

```text
Ask backup STATUS
```

and use the sequence reported by the backup.

---

## 8. Interrupted Catch-Up

Example:

```text
Primary = 20
Backup = 15
```

Catch-up:

```text
16
17
18
19
20
```

Suppose:

```text
16 ACK
17 ACK
18 ACK
```

and then the backup crashes.

After restart:

```text
STATUS
```

returns:

```text
LAST_SEQUENCE 18
```

The next catch-up starts from:

```text
18 + 1 = 19
```

and sends:

```text
19
20
```

The backup then reaches:

```text
20
```

and can become:

```text
HEALTHY
```

---

## 9. Never Assume Catch-Up Completed

Incorrect behavior:

```text
send 16
send 17
send 18
send 19
send 20

connection lost

mark HEALTHY
```

Correct behavior:

```text
connection lost
      |
      v
DOWN
      |
      v
reconnect
      |
      v
STATUS
      |
      v
actual sequence
      |
      v
resume catch-up
```

---

## 10. Strict Sequence Validation

The backup expects:

```text
expected = lastSequence + 1
```

If the backup currently has:

```text
18
```

then the next valid record is:

```text
19
```

Therefore:

```text
19 → accept
18 → reject as duplicate
20 → reject as out-of-order
```

This protects the backup against skipped or duplicated replication records.

---

## 11. Duplicate Replay

Suppose:

```text
Backup = 18
```

and the primary sends:

```text
18
```

again.

The backup expects:

```text
19
```

so it must reject the record.

The primary should not blindly retry the same record forever.

Instead:

1. Stop the current catch-up attempt.
2. Check the backup status again.
3. Determine the actual backup sequence.
4. Resume from the correct sequence.

Recovery should therefore be **state-driven**.

---

## 12. Catch-Up Failure States

### Connection failure

```text
catch-up
   ↓
connection failure
   ↓
DOWN
```

### Send failure

```text
catch-up
   ↓
send failure
   ↓
DOWN
```

### Receive/ACK failure

```text
catch-up
   ↓
ACK not received
   ↓
DOWN
```

The exact persisted sequence is determined later through `STATUS`.

### Backup rejects a record

```text
catch-up
   ↓
unexpected response
   ↓
do not mark HEALTHY
   ↓
re-check STATUS
```

The backup remains `LAGGING` until its actual state is known.

---

## 13. Recovery After Catch-Up Failure

Desired state machine:

```text
                 failure
                   |
                   v
              +---------+
              |  DOWN   |
              +---------+
                   |
                reconnect
                   |
                   v
             +-----------+
             | STARTING  |
             +-----------+
                   |
                STATUS
                   |
          +--------+--------+
          |                 |
          v                 v
       equal             behind
          |                 |
          v                 v
      HEALTHY            LAGGING
                            |
                         catch-up
                            |
                            v
                         HEALTHY
```

If catch-up fails again:

```text
LAGGING
   |
   +---- failure ----> DOWN
```

The next health-monitor cycle can retry recovery.

---

## 14. WAL Records Used for Recovery

The primary uses:

```text
getRecordsFromSequence(startSequence)
```

Example:

```text
Primary WAL:

1 SET A 10
2 SET B 20
3 DEL A
4 SET C 30
5 SET D 40
```

If:

```text
Backup = 2
```

then:

```text
startSequence = 3
```

and the primary sends:

```text
3
4
5
```

---

## 15. Recovery Must Use WAL, Not Cache

The primary cache is not the recovery source.

The recovery source is:

```text
Primary WAL
```

This means a key can be recovered even if it is no longer present in the primary's LRU cache.

---

## 16. Concurrent Writes During Catch-Up

Suppose catch-up starts:

```text
Primary = 20
Backup = 15
```

The backup is being brought through:

```text
16 → 17 → 18 → 19 → 20
```

While catch-up is running, a client performs another write:

```text
SET X Y
```

and the primary reaches:

```text
21
```

The backup may finish the current catch-up at:

```text
20
```

It is therefore still:

```text
LAGGING
```

not `HEALTHY`.

The system should verify the current primary sequence after catch-up.

If:

```text
Backup = 20
Primary = 21
```

the backup remains `LAGGING` and the next cycle catches up sequence 21.

This is normal eventual convergence.

---

## 17. Final Verification Rule

After catch-up records are acknowledged:

```text
STATUS
```

must be requested again.

Then:

```text
Backup == Primary
    → HEALTHY

Backup < Primary
    → LAGGING

Backup > Primary
    → DOWN
```

Only an explicitly verified equal sequence allows `HEALTHY`.

---

# 18. Implementation Plan

## 13.1 Catch-Up Progress Verification

Review the existing catch-up implementation.

Ensure:

- Every record requires an ACK.
- Backup progress is never guessed.
- Final status is always checked.
- `HEALTHY` is assigned only after verification.

---

## 13.2 Interrupted Catch-Up Recovery

Test:

```text
Backup behind
      ↓
catch-up starts
      ↓
backup receives several records
      ↓
backup crashes
      ↓
backup restarts
      ↓
STATUS
      ↓
resume from actual sequence
```

No manual data repair should be necessary.

---

## 13.3 Duplicate Protection

Test:

```text
Backup = N

send N again
```

The backup should reject it because:

```text
expected = N + 1
```

Then verify that the system can recover by checking STATUS and resuming correctly.

---

## 13.4 Mid-Catch-Up Failure

Example:

```text
Primary = 30
Backup = 20

Catch-up:
21 ACK
22 ACK
23 ACK
24 ACK

backup crashes
```

After restart:

```text
Backup = 24
```

if those records were persisted.

Recovery should resume:

```text
25 → 30
```

---

## 13.5 Repeated Interrupted Catch-Up

Repeat:

```text
catch-up
   ↓
failure
   ↓
recovery
   ↓
catch-up
   ↓
failure
   ↓
recovery
```

The system should eventually converge.

---

## 13.6 Concurrent Writes During Catch-Up

Test:

```text
Primary = 20
Backup = 15

start catch-up

while catch-up runs:
    SET key1 value1
    SET key2 value2
    DEL key3
```

The backup may temporarily remain:

```text
LAGGING
```

until the newly created WAL records are replayed.

Final condition:

```text
Backup sequence == Primary sequence
```

---

# 19. Failure Test Matrix

| Scenario | Expected Result |
|---|---|
| Backup already healthy | No catch-up |
| Backup behind | LAGGING → catch-up |
| Backup catches up | HEALTHY |
| Backup fails before catch-up | DOWN |
| Backup fails during catch-up | DOWN |
| Backup restarts | STARTING |
| Backup has partial catch-up | Resume from actual sequence |
| Duplicate sequence received | Reject |
| Missing sequence received | Reject |
| Backup ahead of primary | DOWN |
| New writes during catch-up | Remain LAGGING until caught up |
| Final sequence equal | HEALTHY |
| Final sequence lower | LAGGING |
| Repeated failure/recovery | Eventually converges |

---

# 20. Scope Boundary

Phase 13 does **not** introduce:

- Raft
- leader election
- quorum writes
- consensus
- distributed transactions
- multiple primaries
- automatic primary promotion

The architecture remains:

```text
             PRIMARY
                |
        +-------+-------+
        |               |
      9002            9003
```

The primary remains the source of truth.

---

# 21. Completion Criteria

Phase 13 is complete when:

- [ ] Catch-up uses the primary WAL.
- [ ] Backup progress comes from its actual persisted sequence.
- [ ] Interrupted catch-up can resume.
- [ ] Duplicate records are rejected safely.
- [ ] Out-of-order records are rejected safely.
- [ ] Mid-catch-up failure results in `DOWN`.
- [ ] Restart transitions through `STARTING`.
- [ ] Partial progress is detected through `STATUS`.
- [ ] Catch-up resumes from the correct sequence.
- [ ] Concurrent writes are eventually caught up.
- [ ] Final sequence is verified.
- [ ] `HEALTHY` is never assigned without sequence verification.
- [ ] Repeated failure/recovery eventually converges.

---

# 22. Final Architecture

```text
                         PRIMARY
                    +----------------+
                    |   Primary WAL  |
                    |   Sequence N   |
                    +-------+--------+
                            |
                 +----------+----------+
                 |                     |
                 v                     v
          +-------------+       +-------------+
          |   Backup    |       |   Backup    |
          |    9002     |       |    9003     |
          +------+------+       +------+------+
                 |                     |
              Own WAL               Own WAL
                 |                     |
                 v                     v
          Actual Sequence       Actual Sequence
                 |                     |
                 +----------+----------+
                            |
                     Health Monitor
                            |
                  +---------+---------+
                  |                   |
               LAGGING              DOWN
                  |                   |
               Catch-up            Reconnect
                  |                   |
                  +---------+---------+
                            |
                         STATUS
                            |
                            v
                     Sequence Check
                            |
                  +---------+---------+
                  |                   |
                Equal              Behind
                  |                   |
               HEALTHY             LAGGING
```

---

# 23. Phase 13 Summary

The key principle is:

> **Never assume a backup completed catch-up. Always ask the backup what sequence it actually persisted.**

Desired recovery:

```text
Failure
   ↓
DOWN
   ↓
Reconnect
   ↓
STARTING
   ↓
Read actual WAL sequence
   ↓
LAGGING
   ↓
Replay missing WAL
   ↓
Verify actual sequence
   ↓
HEALTHY
```

This makes backup recovery resilient even when the recovery process itself fails.
