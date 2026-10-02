# Distributed Sharded Key-Value Store

A Redis-like distributed key-value store built from scratch in **C++17** — using raw TCP sockets, OS-level threading, custom sharding, primary-backup replication, Write-Ahead Logging, and a thread-safe LRU cache.

The system is organized as a small distributed cluster with **2 independent shards, 2 primaries, and 4 backup nodes**. A router deterministically maps every key to a shard using 64-bit FNV-1a hashing.

Built as a systems project to demonstrate practical concepts in **data structures, concurrency, networking, persistence, sharding, replication, failure recovery, and performance engineering**.

---

## What It Does

Clients connect to a single router over TCP and send simple text commands:

```text
SET city Chennai       → ADDED
GET city               → VALUE: Chennai
DEL city               → DELETED
GET missing            → NOT_FOUND
```

The router determines which shard owns the key and forwards the request to that shard's primary.

Every mutation is persisted through a sequence-numbered WAL and replicated to the shard's healthy backups.

The system supports:

- Deterministic key-based sharding
- Primary-backup replication
- Per-shard WALs
- WAL replay and recovery
- Thread-safe LRU caching
- Concurrent clients
- Backup failure and recovery
- Read fallback when a primary is unavailable
- Replica catch-up from WAL sequence numbers
- Independent failure domains for each shard

---

# Architecture

```text
                              ┌──────────────────┐
                              │    Client(s)     │
                              └────────┬─────────┘
                                       │
                                       │ TCP
                                       ▼
                              ┌──────────────────┐
                              │   Router :8080   │
                              │                  │
                              │ FNV-1a(key) % 2 │
                              └────────┬─────────┘
                                       │
                     ┌─────────────────┴─────────────────┐
                     │                                   │
                     ▼                                   ▼
             ┌────────────────┐                  ┌────────────────┐
             │    Shard 0     │                  │    Shard 1     │
             │  Primary 9001  │                  │  Primary 9011  │
             │      WAL       │                  │      WAL       │
             │    LRU Cache   │                  │    LRU Cache   │
             └───────┬────────┘                  └───────┬────────┘
                     │                                   │
              ┌──────┴──────┐                     ┌──────┴──────┐
              │             │                     │             │
              ▼             ▼                     ▼             ▼
          Backup 9002   Backup 9003          Backup 9012   Backup 9013
```

### Cluster topology

| Node | Port | Role |
|---|---:|---|
| Router | `8080` | Request routing |
| Shard 0 Primary | `9001` | Primary storage |
| Shard 0 Backup | `9002` | Replica |
| Shard 0 Backup | `9003` | Replica |
| Shard 1 Primary | `9011` | Primary storage |
| Shard 1 Backup | `9012` | Replica |
| Shard 1 Backup | `9013` | Replica |

Each shard is an independent replication group.

---

# Request Flow

A request follows this path:

```text
Client
  │
  ▼
Router :8080
  │
  │ FNV-1a(key) % 2
  ▼
ShardManager
  │
  ▼
ShardRouter
  │
  ▼
Shard Primary
  │
  ├── LRU Cache
  ├── WAL
  └── Replication
       ├── Backup 1
       └── Backup 2
```

The router contains topology information but does **not** store application data.

For `GET`, the router first contacts the shard primary. If the primary is unavailable, it can read from a healthy backup.

For `SET` and `DEL`, the primary is required.

---

# Sharding

Keys are distributed using the 64-bit **FNV-1a** hash:

```cpp
shard = fnv1a(key) % shardCount;
```

With the current two-shard cluster:

```text
shard = FNV1a(key) % 2
```

This gives deterministic routing without maintaining a central key-to-shard table.

For example, during testing:

```text
phase18_s0 -> Shard 1 -> Primary :9011
phase18_s1 -> Shard 0 -> Primary :9001
```

The name of a key does not determine its shard — the hash does.

### Why FNV-1a?

- Simple
- Fast
- Deterministic
- Good distribution for this workload
- Produces a 64-bit hash
- Requires no external library

---

# Write Path

For a mutation such as:

```text
SET city Chennai
```

the logical flow is:

```text
Client
  │
  ▼
Router
  │
  ▼
Shard Primary
  │
  ├── Assign WAL sequence
  ├── Persist mutation
  ├── Update local state/cache
  └── Replicate to healthy backups
          │
          ├── Backup 1
          └── Backup 2
```

Each primary maintains its own sequence-number stream.

Example:

```text
4406 SET city Chennai
4407 SET lang C++
4408 DEL lang
```

The sequence number lets backups detect missing or out-of-order mutations.

---

# Write-Ahead Log

Every primary and backup maintains a persistent WAL.

Primary WALs:

```text
files/shard-0-primary.wal
files/shard-1-primary.wal
```

Backup WALs:

```text
files/backup-9002.wal
files/backup-9003.wal
files/backup-9012.wal
files/backup-9013.wal
```

A WAL record contains:

```text
sequence number
operation
key
value
```

Supported mutation types:

```text
SET
DEL
```

The WAL is used for:

- Durable mutation history
- Startup recovery
- Sequence tracking
- Replica catch-up
- Reconstructing state

The WAL is append-oriented and sequence-numbered.

> WAL files should not be deleted or cleared during normal testing.

---

# Primary-Backup Replication

Each shard has:

```text
                 Primary
                /       \
               /         \
          Backup 1     Backup 2
```

The primary tracks the health and sequence state of each backup.

A healthy backup receives replicated mutations.

A backup that is unavailable or behind is excluded from the normal live replication path until it catches up.

This keeps one unhealthy replica from blocking the entire shard.

---

# Replica Catch-Up

Suppose Shard 0 is at:

```text
Primary: 105
Backup 9002: 101
Backup 9003: 105
```

Backup `9002` is missing:

```text
102
103
104
105
```

The primary can use its WAL to replay the missing records.

```text
Primary WAL
   │
   ├── seq 102
   ├── seq 103
   ├── seq 104
   └── seq 105
             │
             ▼
        Backup 9002
```

After catch-up:

```text
Primary:      105
Backup 9002:  105
Backup 9003:  105
```

The same mechanism is used when a backup is restarted after being unavailable.

---

# Backup Health

The replication layer tracks replica state.

Conceptually:

```text
HEALTHY
LAGGING
DOWN
STARTING
```

The health-monitoring logic periodically checks replica availability and sequence state.

A recovered backup can be synchronized from the WAL before returning to normal replication.

This was validated experimentally by:

```text
1. Stopping a backup
2. Continuing writes
3. Restarting the backup
4. Verifying missing data appeared on the recovered backup
```

---

# Failure Handling

## Backup failure

If one backup goes down:

```text
Primary
  ├── Backup 1  DOWN
  └── Backup 2  HEALTHY
```

the shard remains operational through the primary and remaining healthy replica.

When the failed backup returns, it can catch up from the WAL.

## Primary failure

If a primary goes down:

```text
GET
```

can be served from a healthy backup.

Writes are rejected while the primary is unavailable because this implementation does not perform automatic primary election.

After the primary is restarted, its WAL allows state recovery.

## Shard isolation

Shard 0 and Shard 1 have independent storage and replication groups.

A failure in Shard 0 does not corrupt the data or stop normal operation of Shard 1.

---

# LRU Cache

The storage layer uses a thread-safe LRU cache.

Internally:

```text
std::list<pair<string,string>>
+
std::unordered_map<string, iterator>
```

The list maintains:

```text
MRU -> LRU
```

The hashmap provides direct access to the list iterator.

That gives average:

```text
GET  O(1)
SET  O(1)
DEL  O(1)
```

Current cache capacity:

```text
1000
```

The cache is protected by a mutex so the list and hashmap remain consistent.

Capacity behavior:

```text
capacity == 0   -> cache disabled
capacity < 0    -> invalid_argument
capacity > 0    -> normal LRU
```

---

# Thread Safety

The system supports concurrent client connections.

The router and storage nodes use threads to handle independent client connections.

Shared structures are protected where required:

```text
LRU cache
WAL
replication state
backup state
shard configuration
```

The router does not serialize every request through one global request lock, allowing different clients and shards to make progress concurrently.

---

# TCP Stream Handling

TCP is a byte stream rather than a message protocol.

A single command can arrive in multiple pieces:

```text
recv #1:
SET pha

recv #2:
se18_s0 value\n
```

The server therefore maintains a receive buffer and processes complete newline-delimited commands.

This also allows multiple commands to arrive in a single TCP read.

---

# Network Reliability

The implementation includes:

- `EINTR` retry handling
- Connection timeouts
- Send/receive timeouts
- `SO_NOSIGPIPE`
- Newline-delimited framing
- Bounded command buffers
- Persistent client connections
- Non-blocking connection establishment with timeout

These protections make repeated failure/recovery and concurrent testing more reliable.

---

# Project Structure

```text
distributed-kv-store/
│
├── src/
│   ├── server.cpp
│   ├── backup_server.cpp
│   │
│   ├── replication.cpp
│   ├── replication.h
│   │
│   ├── wal.cpp
│   ├── wal.h
│   │
│   ├── lru_cache.cpp
│   ├── lru_cache.h
│   │
│   ├── shard_manager.cpp
│   ├── shard_manager.h
│   │
│   ├── shard_router.cpp
│   ├── shard_router.h
│   │
│   ├── kv_router.cpp
│   ├── kv_router.h
│   │
│   └── router_server.cpp
│
├── files/
│   ├── shard-0-primary.wal
│   ├── shard-1-primary.wal
│   ├── backup-9002.wal
│   ├── backup-9003.wal
│   ├── backup-9012.wal
│   └── backup-9013.wal
│
└── 4_benchmark_scaling.py
```

### Component responsibilities

| Component | Responsibility |
|---|---|
| `server.cpp` | Primary node |
| `backup_server.cpp` | Backup node |
| `replication.*` | Replica communication and health |
| `wal.*` | Persistent mutation log |
| `lru_cache.*` | Thread-safe cache |
| `shard_manager.*` | Key → shard mapping |
| `shard_router.*` | Shard routing |
| `kv_router.*` | SET/GET/DEL routing |
| `router_server.cpp` | Client-facing router |

---

# Build

## Primary

```bash
g++ -std=c++17 -pthread \
src/server.cpp \
src/replication.cpp \
src/wal.cpp \
src/lru_cache.cpp \
-o primary_node
```

## Backup

```bash
g++ -std=c++17 -pthread \
src/backup_server.cpp \
src/wal.cpp \
src/lru_cache.cpp \
-o backup_node
```

## Router

```bash
g++ -std=c++17 -pthread \
src/router_server.cpp \
src/shard_manager.cpp \
src/shard_router.cpp \
src/kv_router.cpp \
-o router
```

---

# Running the Full Cluster

Start backups first.

### Shard 0 backups

```bash
./backup_node 9002
./backup_node 9003
```

### Shard 1 backups

```bash
./backup_node 9012
./backup_node 9013
```

### Shard 0 primary

```bash
KV_WAL_PATH=files/shard-0-primary.wal \
./primary_node 9001 9002 9003
```

### Shard 1 primary

```bash
KV_WAL_PATH=files/shard-1-primary.wal \
./primary_node 9011 9012 9013
```

### Router

```bash
./router
```

---

# Command Reference

| Command | Example | Response |
|---|---|---|
| `SET` | `SET city Chennai` | `ADDED` |
| `GET` | `GET city` | `VALUE: Chennai` |
| `DEL` | `DEL city` | `DELETED` |
| `PING` | `PING` | `PONG` |

Example:

```bash
printf "SET city Chennai\n" | nc 127.0.0.1 8080
```

```text
ADDED
```

Then:

```bash
printf "GET city\n" | nc 127.0.0.1 8080
```

```text
VALUE: Chennai
```

---

# Testing

The project was validated through staged testing from sharding through the final failure/recovery checks.

## Phase 14 — Sharding

10,000-key distribution:

```text
Shard 0: 4999  (49.99%)
Shard 1: 5001  (50.01%)
Routing errors: 0
```

Result:

```text
PASSED
```

---

## Phase 15 — Performance

### Baseline

```text
Operations:  2000
Success:     2000
Failures:    0

Time:        0.840 s
Throughput:  2379.74 ops/sec
Average:     0.419 ms
p50:         0.345 ms
p95:         0.690 ms
p99:         1.075 ms
```

### 4 Concurrent Clients

```text
Operations:  4000
Success:     4000
Failures:    0

Time:        2.021 s
Throughput:  1979.60 ops/sec
Average:     2.012 ms
p50:         1.580 ms
p95:         3.121 ms
p99:         9.898 ms
```

### Shard workload

| Shard | Operations | Success | Failures | Throughput |
|---|---:|---:|---:|---:|
| Shard 0 | 2000 | 2000 | 0 | 349.71 ops/s |
| Shard 1 | 2000 | 2000 | 0 | 368.47 ops/s |

### Concurrency scaling

| Clients | Operations | Success | Failures | Throughput |
|---:|---:|---:|---:|---:|
| 1 | 2000 | 2000 | 0 | 406.29 ops/s |
| 2 | 2000 | 2000 | 0 | 1354.51 ops/s |
| 4 | 2000 | 2000 | 0 | 2050.02 ops/s |

Result:

```text
PASSED
```

---

# Phase 16 — Router Integration

Validated:

- Deterministic routing
- SET / GET / DEL
- Cross-shard independence
- Concurrent router clients
- 4 concurrent clients
- 800 total operations
- 0 failures

Result:

```text
PASSED
```

---

# Phase 17 — Multiple Shards

Validated:

- Independent shard operation
- Deterministic key routing
- Shard 0 primary failure
- Backup read fallback
- Primary recovery
- Shard 1 unaffected by Shard 0 failure
- Data preserved after recovery

Result:

```text
PASSED
```

---

# Phase 18 — 6-Node Deployment

Validated the complete:

```text
Router
  |
  +-- Shard 0
  |    +-- Primary 9001
  |    +-- Backup 9002
  |    +-- Backup 9003
  |
  +-- Shard 1
       +-- Primary 9011
       +-- Backup 9012
       +-- Backup 9013
```

Verified:

- All 6 storage nodes simultaneously
- Cross-shard SET/GET
- Primary-to-backup replication
- Both backups per shard
- DELETE propagation
- Cross-shard isolation

Result:

```text
PASSED
```

---

# Phase 19 — Failure Testing

### Shard 0 backup failure

```text
9002 DOWN
9003 remained operational
9002 recovered
9002 caught up
```

### Shard 1 backup failure

```text
9012 DOWN
9013 remained operational
9012 recovered
9012 caught up
```

### Shard 0 primary failure

```text
9001 DOWN
Reads served through healthy backup
Shard 1 remained operational
9001 recovered
```

### Post-recovery write

```text
SET phase19_final recovery_ok
GET phase19_final
```

Result:

```text
ADDED
VALUE: recovery_ok
```

Result:

```text
PASSED
```

---

# Phase 20 — Final Validation

Final sanity test:

```text
SET
GET
DEL
GET
```

Result:

```text
ADDED
VALUE: final_value
DELETED
NOT_FOUND
```

Result:

```text
PASSED
```

---

# Complexity

### Shard selection

For key length `L`:

```text
O(L)
```

### LRU operations

Average:

```text
GET  O(1)
SET  O(1)
DEL  O(1)
```

### Replication

For `R` healthy replicas:

```text
O(R)
```

network replication operations per mutation.

### WAL replay

For `N` WAL records:

```text
O(N)
```

---

# Key Systems Concepts Demonstrated

| Concept | Implementation |
|---|---|
| Sharding | FNV-1a + fixed shard count |
| Routing | Router + ShardManager + ShardRouter |
| Replication | Primary-backup |
| Persistence | Append-oriented WAL |
| Recovery | WAL replay + replica catch-up |
| Caching | Thread-safe LRU |
| Concurrency | Multi-threaded TCP servers |
| TCP framing | Newline-delimited receive buffers |
| Failure handling | Replica health states + read fallback |
| Sequence ordering | Per-shard WAL sequence numbers |
| Performance | Throughput + p50/p95/p99 benchmarks |

---

# Final Cluster Status

```text
                DISTRIBUTED KV STORE

                    Router :8080
                         |
              ┌──────────┴──────────┐
              │                     │
          Shard 0               Shard 1
          Primary               Primary
           :9001                 :9011
          /     \               /     \
       :9002   :9003         :9012   :9013
       Backup  Backup         Backup  Backup

       2 SHARDS
       2 PRIMARIES
       4 BACKUPS
       6 STORAGE NODES
```

## Project Status

```text
SHARDED
REPLICATED
WAL-BACKED
RECOVERABLE
CONCURRENT
FAILURE-TESTED
PERFORMANCE-TESTED

PROJECT COMPLETE
```

---

## Why This Project

Most application projects place the difficult systems work behind an external database or framework.

This project implements the core storage infrastructure itself:

- networking
- request routing
- sharding
- persistence
- caching
- replication
- concurrency
- failure recovery

The goal is not only to make `SET` and `GET` work, but to demonstrate the reasoning behind the system: how keys are partitioned, how mutations become durable, how replicas stay synchronized, and what happens when nodes fail and recover.

Built in **C++17** with raw TCP networking and standard-library data structures.
