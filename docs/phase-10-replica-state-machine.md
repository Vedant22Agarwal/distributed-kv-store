# Phase 10 — Replica State Machine

## Overview

Phase 10 builds on the replication, catch-up, health tracking, and read-routing work completed in Phase 9.

The goal is to make the lifecycle of every backup replica explicit.

The state machine will be:

```text
                  STARTING
                      |
                      v
                  HEALTHY
                 /       \
                /         \
          replication    connection
             gap          failure
              |              |
              v              v
           LAGGING          DOWN
              |              |
           catch-up       reconnect
              |              |
              +-------> STARTING
```

A successful synchronization from `STARTING` or `LAGGING` leads back to `HEALTHY`.

---

# 1. Why We Need a State Machine

Phase 9 introduced:

```cpp
enum class ReplicaState
{
    HEALTHY,
    LAGGING,
    DOWN
};
```

This allowed the primary to decide whether a backup could serve reads.

A real distributed system also needs to represent:

- a newly registered replica,
- a replica reconnecting after failure,
- a replica that is catching up,
- a synchronized replica,
- a failed replica.

The state machine gives deterministic rules for these transitions.

---

# 2. Replica States

We will use four states:

```cpp
enum class ReplicaState
{
    STARTING,
    HEALTHY,
    LAGGING,
    DOWN
};
```

## STARTING

The replica is registered, but its synchronization status has not yet been confirmed.

Typical situations:

- server startup,
- newly registered backup,
- backup reconnects after failure.

A `STARTING` replica must not serve reads.

## HEALTHY

The backup is reachable and synchronized.

Condition:

```text
backupSequence == primarySequence
```

A healthy replica can serve reads.

## LAGGING

The backup is reachable but behind the primary.

Condition:

```text
backupSequence < primarySequence
```

A lagging replica must not serve reads.

It should be caught up first.

## DOWN

The primary cannot communicate with the replica.

Possible causes:

- backup process stopped,
- network failure,
- socket connection failure,
- backup crash.

A DOWN replica must not serve reads.

When it becomes reachable again, it should enter `STARTING` first rather than immediately becoming `HEALTHY`.

---

# 3. State Transition Diagram

```text
                         +-----------+
                         |  STARTING |
                         +-----+-----+
                               |
                    status/synchronization
                               |
                  +------------+------------+
                  |                         |
                  v                         v
            +-----------+              +-----------+
            |  HEALTHY  |              |  LAGGING  |
            +-----+-----+              +-----+-----+
                  |                          |
                  | failure                  | catch-up
                  v                          |
            +-----------+                    |
            |    DOWN   |                    |
            +-----+-----+                    |
                  |                          |
                  | reconnect                |
                  v                          |
            +-----------+                    |
            |  STARTING |<-------------------+
            +-----------+
```

Important rules:

1. Only `HEALTHY` replicas serve reads.
2. `LAGGING` replicas must catch up before serving reads.
3. `DOWN` replicas cannot serve reads.
4. Reconnecting replicas enter `STARTING`.
5. A replica becomes `HEALTHY` only after synchronization is confirmed.

---

# 4. Replica Lifecycle

Consider a new backup on port `9003`.

Initially:

```text
STARTING
```

The primary checks:

```text
STATUS
```

Suppose:

```text
Primary = 15
Backup = 15
```

Then:

```text
STARTING -> HEALTHY
```

The backup can now serve reads.

---

# 5. Replica Falls Behind

Suppose:

```text
Primary = 17
Backup = 15
```

The backup is behind.

Its state becomes:

```text
HEALTHY -> LAGGING
```

The backup must not serve reads.

The primary obtains missing WAL records:

```text
16
17
```

and sends them to the backup.

After successful catch-up:

```text
Backup = 17
Primary = 17
```

The state becomes:

```text
LAGGING -> HEALTHY
```

---

# 6. Replica Failure

Suppose:

```text
Primary = 20
Backup = 20
State = HEALTHY
```

The backup process crashes.

The next communication attempt fails.

The primary changes:

```text
HEALTHY -> DOWN
```

The backup is removed from read routing.

A GET request therefore uses another healthy replica or the primary cache.

---

# 7. Replica Recovery

Suppose backup `9003` starts again.

It should not immediately become:

```text
HEALTHY
```

Instead:

```text
DOWN -> STARTING
```

The primary checks its sequence.

Suppose:

```text
Primary = 25
Backup = 20
```

Then:

```text
STARTING -> LAGGING
```

Catch-up begins.

After records `21` through `25` are successfully applied:

```text
Backup = 25
Primary = 25
```

Then:

```text
LAGGING -> HEALTHY
```

This prevents stale data from being served after recovery.

---

# 8. Why STARTING Is Important

A successful TCP connection does not prove that a replica is synchronized.

The correct flow is:

```text
TCP connection
      |
      v
STATUS
      |
      v
Compare sequence numbers
      |
      +----------------+
      |                |
      v                v
Equal             Behind
      |                |
      v                v
 HEALTHY            LAGGING
```

Therefore:

```text
reachable != healthy
```

A replica is healthy only when its state is confirmed.

---

# 9. Read Routing Rules

The read-routing rule is:

```text
GET
 |
 +--> HEALTHY replica available?
          |
       +--+--+
       |     |
      YES    NO
       |     |
       v     v
    Replica Primary
              cache
```

The following states must never serve reads:

```text
STARTING
LAGGING
DOWN
```

Only:

```text
HEALTHY
```

is eligible.

---

# 10. Write Routing Rules

Writes continue to go to the primary.

```text
SET / DEL
     |
     v
  Primary
     |
     +--> WAL
     |
     +--> Replication
             |
             +--> Backup 1
             |
             +--> Backup 2
```

The primary remains responsible for ordering writes and assigning sequence numbers.

---

# 11. Sequence Number as the Synchronization Indicator

Suppose:

```text
Primary sequence = 100
```

Then:

```text
Backup 1 = 100 -> HEALTHY
Backup 2 = 97  -> LAGGING
Backup 3 = unreachable -> DOWN
```

The important invariant is:

```text
HEALTHY
=> backupSequence == primarySequence
```

A healthy replica must not remain healthy after a fresh status check shows that it is behind.

---

# 12. State Transition Table

| Current State | Event | Next State |
|---|---|---|
| STARTING | status succeeds and sequence matches | HEALTHY |
| STARTING | status succeeds and backup is behind | LAGGING |
| STARTING | connection fails | DOWN |
| HEALTHY | sequence remains equal | HEALTHY |
| HEALTHY | backup falls behind | LAGGING |
| HEALTHY | connection failure | DOWN |
| LAGGING | catch-up succeeds | HEALTHY |
| LAGGING | catch-up incomplete | LAGGING |
| LAGGING | connection failure | DOWN |
| DOWN | reconnect succeeds | STARTING |
| DOWN | reconnect fails | DOWN |

---

# 13. Implementation Plan

## Step 1 — Add STARTING

Change:

```cpp
enum class ReplicaState
{
    HEALTHY,
    LAGGING,
    DOWN
};
```

to:

```cpp
enum class ReplicaState
{
    STARTING,
    HEALTHY,
    LAGGING,
    DOWN
};
```

## Step 2 — New Backups Start in STARTING

A newly registered backup should start as:

```cpp
ReplicaState::STARTING
```

rather than `DOWN`.

Reason: the backup is not known to be down; its status simply has not been checked yet.

## Step 3 — Status Determines the State

When:

```text
backupSequence == primarySequence
```

set:

```text
HEALTHY
```

When:

```text
backupSequence < primarySequence
```

set:

```text
LAGGING
```

When communication fails:

```text
DOWN
```

## Step 4 — Catch-up Restores HEALTHY

After successful catch-up:

```text
LAGGING -> HEALTHY
```

The final status check should confirm:

```text
backupSequence == primarySequence
```

## Step 5 — Recovery Starts at STARTING

When a previously DOWN replica becomes reachable again:

```text
DOWN -> STARTING
```

Then its status determines whether it becomes:

```text
STARTING -> HEALTHY
```

or:

```text
STARTING -> LAGGING
```

---

# 14. Failure Scenarios

## Scenario A — One backup fails

```text
Backup 1 -> HEALTHY
Backup 2 -> DOWN
```

Reads can use Backup 1.

Writes continue through the primary.

The failed backup can recover later.

## Scenario B — Both backups fail

```text
Backup 1 -> DOWN
Backup 2 -> DOWN
```

Reads fall back to the primary cache.

Writes continue to the primary WAL and cache.

Replication is repaired when the backups return.

## Scenario C — One backup is lagging

```text
Backup 1 -> HEALTHY
Backup 2 -> LAGGING
```

Reads use Backup 1.

Backup 2 catches up.

After synchronization:

```text
Backup 1 -> HEALTHY
Backup 2 -> HEALTHY
```

## Scenario D — Backup restarts

```text
DOWN
 |
 v
STARTING
 |
 v
LAGGING
 |
 | catch-up
 v
HEALTHY
```

---

# 15. Important Safety Property

The most important property is:

```text
Only HEALTHY replicas serve reads.
```

For example:

```text
Primary:
user = Vedant

Backup:
user = OldValue
```

If the backup is:

```text
LAGGING
```

the primary must never route:

```text
GET user
```

to that backup.

Instead:

```text
GET user
     |
     v
LAGGING replica rejected
     |
     v
Another HEALTHY replica?
     |
     +-- yes -> use it
     |
     +-- no -> primary cache
```

---

# 16. What Phase 10 Gives Us

After Phase 10, the system has an explicit replica lifecycle:

```text
                         PRIMARY
                            |
              +-------------+-------------+
              |                           |
          WRITE PATH                  READ PATH
              |                           |
              v                           v
             WAL                   HEALTHY replica?
              |                      /                       v                    YES          NO
        Replication                 |            |
              |                     v            v
       +------+------+           Backup       Primary
       |             |                          cache
       v             v
   Replica 1      Replica 2
       |             |
       v             v
 State Machine    State Machine
```

---

# 17. Phase 10 Completion Criteria

- [ ] `STARTING` state exists.
- [ ] New replicas begin in `STARTING`.
- [ ] Synchronized replicas become `HEALTHY`.
- [ ] Behind replicas become `LAGGING`.
- [ ] Failed replicas become `DOWN`.
- [ ] Reconnected replicas enter `STARTING`.
- [ ] Lagging replicas catch up.
- [ ] Successful catch-up returns the replica to `HEALTHY`.
- [ ] Only `HEALTHY` replicas serve reads.
- [ ] Primary fallback still works.
- [ ] Existing WAL behavior still works.
- [ ] Existing replication behavior still works.
- [ ] Existing automatic catch-up still works.
- [ ] Failure and recovery scenarios are tested.

---

# 18. Current Project Progress

| Phase | Component | Status |
|---|---|---|
| 1 | Basic KV Store | DONE |
| 2 | LRU Cache | DONE |
| 3 | Thread Safety | DONE |
| 4 | TCP Networking | DONE |
| 5 | Concurrent Server | DONE |
| 6 | TCP Client | DONE |
| 7A | WAL Foundation | DONE |
| 7B | WAL Replay | DONE |
| 7C | WAL + TCP | DONE |
| 8A | Sequence Numbers | DONE |
| 8B | Sequence Validation | DONE |
| 9A | Push Replication | DONE |
| 9B | Replica Catch-up | DONE |
| 9C | Read Routing | DONE |
| 10 | Replica State Machine | NEXT |

---

## Next Implementation Step

The next coding task is:

**Phase 10.1 — introduce the `STARTING` state and update replica initialization.**

Do not start sharding yet. The replica lifecycle should be stable before moving to health monitoring and later sharding.
