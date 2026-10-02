# Phase 11 — Robust Health Monitoring

## 1. Overview

Phase 10 introduced the replica state machine and automatic recovery:

```text
HEALTHY
   │
   │ failure
   ▼
 DOWN
   │
   │ backup returns
   ▼
STARTING
   │
   │ sequence check
   ├───────────────┐
   ▼               ▼
HEALTHY         LAGGING
                   │
                   │ catch-up
                   ▼
                HEALTHY
```

Phase 11 improves the health-monitoring subsystem so that this behavior is reliable, controlled, thread-safe, and easier to observe.

The goal is not to introduce a new replication mechanism. The existing replication, WAL, sequence-number, and catch-up mechanisms remain the foundation.

---

# 2. Why Phase 11 Is Needed

The current health-monitoring implementation periodically calls:

```cpp
getBackupStatus()
```

for every backup.

If a backup is behind, it then calls:

```cpp
catchUpBackup()
```

This works, but there are several areas that need improvement.

### Current limitations

1. Every backup is checked at the same interval regardless of state.
2. A lagging backup may trigger repeated catch-up attempts.
3. Network operations happen inside the monitoring thread.
4. State transitions and monitoring decisions are mixed together.
5. Logging can become repetitive.
6. Shutdown needs to be handled cleanly.
7. Thread synchronization needs to remain correct as the project becomes more concurrent.
8. Health monitoring should have a clear responsibility separate from replication itself.

Phase 11 addresses these issues.

---

# 3. Goals

By the end of Phase 11, the system should have:

- Periodic replica health checks.
- Clear handling of `HEALTHY`, `LAGGING`, `STARTING`, and `DOWN`.
- Controlled automatic catch-up.
- No unnecessary repeated catch-up attempts.
- Thread-safe access to replica state.
- Clean health-monitoring thread startup.
- Clean health-monitoring thread shutdown.
- Useful state-transition logs.
- A monitoring interval that can be changed easily.
- No impact on normal client GET/SET/DEL operations.

---

# 4. Architecture

The architecture after Phase 11 is:

```text
                    ┌──────────────────────┐
                    │    Primary Server    │
                    │                      │
                    │      WAL             │
                    │       │              │
                    │       ▼              │
                    │ ReplicationManager   │
                    └──────────┬───────────┘
                               │
                    ┌──────────▼───────────┐
                    │ Health Monitor Thread│
                    │                       │
                    │ periodic checks       │
                    │ state decisions       │
                    │ recovery trigger      │
                    └──────────┬────────────┘
                               │
                 ┌─────────────┴─────────────┐
                 │                           │
          ┌──────▼──────┐             ┌──────▼──────┐
          │   Backup 1  │             │   Backup 2  │
          │    :9002    │             │    :9003    │
          └─────────────┘             └─────────────┘
```

The health monitor observes replica state. It does not replace the WAL or replication manager.

---

# 5. Replica States

The four states are:

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

The backup has become reachable again and its current status is being determined.

Example:

```text
DOWN
  ↓
backup becomes reachable
  ↓
STARTING
```

The backup should not immediately be treated as a fully synchronized replica.

---

## HEALTHY

The backup is reachable and has the same sequence number as the primary.

Invariant:

```text
backupSequence == primarySequence
```

A healthy replica may participate in read routing.

---

## LAGGING

The backup is reachable but behind the primary.

Invariant:

```text
backupSequence < primarySequence
```

A lagging replica should not be used for reads that require the latest state.

The monitoring system can trigger catch-up.

---

## DOWN

The backup cannot currently be contacted successfully.

Examples:

- connection failure
- STATUS request failure
- STATUS response failure

A DOWN replica should not receive normal reads or replication requests until it becomes reachable again.

---

# 6. State Transition Model

The intended transitions are:

```text
                    connection failure
             ┌─────────────────────────────┐
             │                             │
             ▼                             │
        ┌─────────┐                         │
        │  DOWN   │◄────────────────────────┤
        └────┬────┘                         │
             │                              │
             │ connection succeeds          │
             ▼                              │
        ┌──────────┐                         │
        │ STARTING │                         │
        └────┬─────┘                         │
             │                               │
             │ sequence check                │
       ┌─────┴──────────┐                    │
       │                │                    │
       ▼                ▼                    │
   HEALTHY          LAGGING                  │
       │                │                    │
       │                │ catch-up failure  │
       │                └────────────────────┘
       │
       │ health check fails
       └────────────────────────────────────► DOWN

LAGGING
   │
   │ successful catch-up
   ▼
HEALTHY
```

The important invariant is:

```text
HEALTHY ⇒ backupSequence == primarySequence
```

---

# 7. Health Check Cycle

The monitor runs periodically.

Conceptually:

```text
start
  │
  ▼
check backup
  │
  ├── unreachable ──► DOWN
  │
  └── reachable
         │
         ▼
      STATUS
         │
         ▼
   compare sequences
         │
    ┌────┼────┐
    ▼    ▼    ▼
 HEALTHY LAGGING DOWN
            │
            ▼
        catch-up
```

After all backups are processed:

```text
sleep
  │
  ▼
repeat
```

---

# 8. Monitoring Interval

The current implementation uses a fixed delay such as:

```cpp
this_thread::sleep_for(
    chrono::seconds(5));
```

The exact value should be easy to change.

For this project, a small interval such as 5 seconds is useful for development and testing.

The monitor should not run in a tight loop because that would cause:

- unnecessary network traffic
- excessive CPU usage
- repeated status requests
- noisy logs

---

# 9. Health Check Responsibilities

The health monitor should be responsible for:

### 1. Detecting failures

```text
HEALTHY → DOWN
```

when the backup becomes unreachable.

### 2. Detecting recovery

```text
DOWN → STARTING
```

when the backup becomes reachable again.

### 3. Determining synchronization

```text
STARTING → HEALTHY
```

or:

```text
STARTING → LAGGING
```

based on sequence numbers.

### 4. Triggering recovery

```text
LAGGING → catchUpBackup()
```

### 5. Confirming recovery

After successful catch-up:

```text
LAGGING → HEALTHY
```

---

# 10. Catch-Up Control

A major Phase 11 improvement is avoiding uncontrolled repeated catch-up.

Suppose:

```text
Primary = sequence 100
Backup  = sequence 50
```

The monitor detects:

```text
LAGGING
```

and starts catch-up.

While that catch-up is running, another health-check cycle should not start another catch-up for the same backup.

Conceptually:

```text
LAGGING
   │
   ▼
CATCHING_UP
   │
   ├── success → HEALTHY
   │
   └── failure → LAGGING / DOWN
```

The existing four-state model does not necessarily require exposing `CATCHING_UP` as a public replica state. An internal flag can be used instead.

For example:

```cpp
bool catchingUp;
```

This prevents overlapping catch-up operations.

---

# 11. Thread Safety

The project already uses:

```cpp
mutex replicationMutex;
```

Replica metadata such as:

```cpp
backup.state
backup.lastSequenceNumber
```

must be accessed carefully because:

- client threads may perform replication
- the health-monitoring thread may inspect/update state
- GET routing may inspect replica state
- catch-up may update replica state

The goal is to avoid data races.

A useful rule is:

> Protect replica metadata with the replication mutex, but avoid holding the mutex during long network operations whenever practical.

Network operations can block.

Therefore, future improvements should avoid unnecessarily doing:

```cpp
lock(mutex);
network operation;
network operation;
network operation;
unlock(mutex);
```

A better design is:

```text
lock
read/copy metadata
unlock

perform network operation

lock
update metadata
unlock
```

This keeps the critical section small.

---

# 12. Health Monitoring and Client Requests

The health monitor must not prevent normal client operations.

For example, while the monitor is checking backup 9002:

```text
Client → Primary
        SET key value
```

should still be processed.

Likewise:

```text
Client → Primary
        GET key
```

should not unnecessarily wait for a slow health-check network operation.

This becomes especially important once the project has multiple shards and many concurrent clients.

---

# 13. Logging

Health-monitoring logs should describe meaningful state changes.

Useful messages include:

```text
Backup 127.0.0.1:9002 is DOWN
```

```text
Backup 127.0.0.1:9002 is STARTING
```

```text
Backup 127.0.0.1:9002 is LAGGING
```

```text
Backup 127.0.0.1:9002 catch-up started
```

```text
Backup 127.0.0.1:9002 catch-up completed
```

```text
Backup 127.0.0.1:9002 is HEALTHY
```

Repeated logs should eventually be reduced.

For example, if a backup remains DOWN for five monitoring cycles, printing:

```text
DOWN
DOWN
DOWN
DOWN
DOWN
```

is unnecessary.

Instead, log when the state changes:

```text
HEALTHY → DOWN
```

and then remain quiet until:

```text
DOWN → STARTING
```

or another meaningful transition occurs.

---

# 14. State Transition Logging

A useful helper can eventually be introduced:

```cpp
void setReplicaState(
    BackupNode &backup,
    ReplicaState newState);
```

Its responsibility would be:

1. Compare old state and new state.
2. Update the state.
3. Print a message only when the state changes.

Conceptually:

```cpp
if (backup.state != newState)
{
    cout << "Replica "
         << backup.host << ":"
         << backup.port
         << " state changed"
         << endl;

    backup.state = newState;
}
```

This keeps state transitions centralized.

---

# 15. Failure Scenarios

## Scenario 1 — Backup crashes

```text
HEALTHY
   ↓
connection fails
   ↓
DOWN
```

Primary continues operating.

---

## Scenario 2 — Backup restarts with same WAL

```text
DOWN
  ↓
STARTING
  ↓
same sequence
  ↓
HEALTHY
```

No catch-up is required.

---

## Scenario 3 — Backup restarts behind primary

```text
DOWN
  ↓
STARTING
  ↓
sequence is smaller
  ↓
LAGGING
  ↓
catch-up
  ↓
HEALTHY
```

---

## Scenario 4 — Catch-up fails

```text
LAGGING
   ↓
catch-up
   ↓
failure
```

The replica should remain unavailable for reads and be retried later.

Depending on the exact failure, the state may remain `LAGGING` or become `DOWN`.

---

## Scenario 5 — Primary continues writing while backup is down

Example:

```text
Backup sequence = 20
Primary sequence = 20
```

Backup goes down.

Primary performs:

```text
SET A
SET B
SET C
```

Now:

```text
Primary = 23
Backup  = 20
```

After recovery:

```text
STARTING
   ↓
LAGGING
   ↓
catch-up records 21–23
   ↓
HEALTHY
```

---

# 16. Read Routing Rules

The existing read policy remains:

```text
GET
 │
 ▼
Healthy synchronized replica?
 │
 ├── yes → read replica
 │
 └── no
       ↓
     primary
```

A replica in:

```text
STARTING
LAGGING
DOWN
```

should not be selected as a healthy read replica.

Only:

```text
HEALTHY
```

with:

```text
backupSequence == primarySequence
```

should be eligible.

---

# 17. Write Routing Rules

Writes continue to originate at the primary.

For:

```text
SET
DEL
```

the primary:

```text
1. Writes WAL
2. Flushes WAL
3. Replicates to backups
4. Updates primary cache
```

The health monitor does not become the write path.

This separation is important:

```text
Client writes
     ↓
Primary
     ↓
WAL
     ↓
ReplicationManager
```

while:

```text
HealthMonitor
     ↓
observes replicas
     ↓
triggers recovery
```

---

# 18. Graceful Shutdown

The health-monitoring thread must be stopped before the `ReplicationManager` is destroyed.

Conceptually:

```cpp
healthMonitoring = false;

if (healthThread.joinable())
{
    healthThread.join();
}
```

This prevents:

- `std::terminate`
- background access to destroyed objects
- dangling references
- undefined behavior during shutdown

The server should eventually call:

```cpp
replicationManager.stopHealthMonitoring();
```

before exiting.

The destructor can also provide a safety net.

---

# 19. Implementation Plan

Phase 11 will be implemented incrementally.

### Step 11.1 — Monitoring configuration

Introduce a clean monitoring interval.

### Step 11.2 — Centralized state transitions

Create a helper for state updates and transition logging.

### Step 11.3 — Thread-safe replica metadata

Review access to:

```cpp
state
lastSequenceNumber
```

and reduce unnecessarily large critical sections.

### Step 11.4 — Controlled catch-up

Prevent multiple catch-up attempts for the same replica from running simultaneously.

### Step 11.5 — Better failure handling

Handle:

- connection failure
- STATUS failure
- malformed responses
- catch-up failure
- recovery

consistently.

### Step 11.6 — Graceful shutdown

Ensure the monitor thread stops cleanly.

### Step 11.7 — Failure testing

Test:

1. Healthy backup.
2. Backup crash.
3. Backup restart.
4. Restart with same WAL.
5. Restart behind primary.
6. Multiple writes while backup is down.
7. Catch-up.
8. Catch-up failure.
9. Multiple replicas failing.
10. Primary continuing to serve clients during recovery.

---

# 20. Completion Criteria

Phase 11 is complete when:

- [ ] Health monitoring runs periodically.
- [ ] Replica failures are detected automatically.
- [ ] Recovery is detected automatically.
- [ ] `STARTING` is used correctly.
- [ ] `LAGGING` replicas are detected.
- [ ] Catch-up does not overlap for the same replica.
- [ ] Replica metadata is thread-safe.
- [ ] State-change logging is clean.
- [ ] Monitoring can be stopped cleanly.
- [ ] Client requests continue during monitoring.
- [ ] Multiple replicas can be monitored independently.
- [ ] Failure and recovery tests pass.

---

# 21. Expected Final Behavior

A complete recovery cycle should look like:

```text
                 ┌─────────────┐
                 │   HEALTHY   │
                 └──────┬──────┘
                        │
                  connection
                     failure
                        │
                        ▼
                 ┌─────────────┐
                 │    DOWN     │
                 └──────┬──────┘
                        │
                 connection
                    succeeds
                        │
                        ▼
                 ┌─────────────┐
                 │  STARTING   │
                 └──────┬──────┘
                        │
                 sequence check
                   ┌────┴────┐
                   │         │
                   ▼         ▼
              HEALTHY     LAGGING
                             │
                         catch-up
                             │
                             ▼
                         HEALTHY
```

The primary should remain available throughout the process.

---

# 22. Current Project Status

```text
Phase 1   Basic KV Store             DONE
Phase 2   LRU Cache                  DONE
Phase 3   Thread Safety              DONE
Phase 4   TCP Networking             DONE
Phase 5   Concurrent Server          DONE
Phase 6   TCP Client                 DONE
Phase 7   WAL                        DONE
Phase 8   Sequence Numbers           DONE
Phase 9   Replication                DONE
Phase 10  Replica State Machine      DONE
Phase 11  Robust Health Monitoring   NEXT
Phase 12  Failure Recovery           LATER
Phase 13  Replica Catch-up           LATER
Phase 14  Sharding                   LATER
Phase 15  FNV-1a Hashing             LATER
Phase 16  Stateless Router           LATER
Phase 17  Multiple Shards            LATER
Phase 18  6-Node Deployment          LATER
Phase 19  Failure Testing            LATER
Phase 20  Performance + Documentation LATER
```

---

# 23. Key Takeaway

Phase 10 made replica recovery **possible**.

Phase 11 makes replica monitoring **robust**.

The distinction is:

```text
Phase 10:
"Can the system recover a failed replica?"

Phase 11:
"Can the system continuously and safely monitor replicas
and manage recovery without interfering with normal traffic?"
```

This prepares the project for the next major architectural step: **sharding**.
