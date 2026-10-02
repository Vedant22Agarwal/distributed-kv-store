# Phase 14 — Sharding

## 1. Overview

Phase 13 completed the replication and recovery subsystem.

The next step is **sharding**.

Replication answers:

> How do we keep multiple copies of the same data?

Sharding answers:

> How do we split the data across multiple storage groups so the system can scale?

Instead of every node storing every key, different shards will own different keys.

---

# 2. Current Architecture

The current system has one primary and two replicas:

```text
                    PRIMARY
                      |
             +--------+--------+
             |                 |
          Backup 1          Backup 2
           9002              9003
```

Every key currently belongs to this single storage group.

Replication provides redundancy, but it does not distribute the storage workload.

---

# 3. Why Sharding?

Suppose the system contains:

```text
10 million keys
```

and every replica stores all 10 million keys.

Adding more replicas improves availability/read capacity, but every replica still needs to store the entire dataset.

With sharding:

```text
              All Keys
                 |
       +---------+---------+
       |                   |
    Shard 0             Shard 1
       |                   |
   Keys A-M             Keys N-Z
```

Each shard owns only part of the dataset.

This provides:

- Larger total storage capacity
- Distribution of write load
- Distribution of data
- Independent replication groups
- Horizontal scalability

---

# 4. Sharding vs Replication

These are different concepts.

## Replication

Replication creates copies:

```text
Shard 0
  |
  +---- Replica A
  |
  +---- Replica B
```

The replicas contain the same data.

## Sharding

Sharding partitions the data:

```text
Shard 0 → subset of keys

Shard 1 → different subset of keys
```

The shards contain different data.

## Together

Our final architecture will combine both:

```text
                  Router
                    |
          +---------+---------+
          |                   |
       Shard 0             Shard 1
          |                   |
     +----+----+         +----+----+
     |         |         |         |
  Primary   Backup    Primary   Backup
```

Eventually each shard can have one primary and multiple backups.

---

# 5. Basic Key-to-Shard Mapping

The router needs a deterministic rule.

Conceptually:

```text
shard = hash(key) % number_of_shards
```

For two shards:

```text
hash(key) % 2
```

produces:

```text
0 → Shard 0
1 → Shard 1
```

Example:

```text
"user:1001" → hash → 8,731,234 → 0
"user:1002" → hash → 4,123,111 → 1
```

The exact values depend on the hash function.

---

# 6. Why Deterministic Mapping Matters

Every request for the same key must select the same shard.

For example:

```text
SET user:42 hello
```

might map to:

```text
Shard 1
```

Later:

```text
GET user:42
```

must also map to:

```text
Shard 1
```

If different requests choose different shards, the system could return incorrect results.

Therefore:

```text
same key
   ↓
same hash
   ↓
same shard
```

---

# 7. Initial Modulo Hashing

For the first implementation we can use:

```cpp
hash(key) % shardCount
```

This is useful for learning the routing architecture.

However, there is an important limitation.

If:

```text
shardCount = 2
```

then:

```text
hash(key) % 2
```

is used.

If we later change to:

```text
shardCount = 3
```

almost every key can move to another shard.

This is one reason production distributed systems often use more advanced partitioning strategies.

---

# 8. FNV-1a

The project will eventually use a 64-bit FNV-1a hash.

FNV-1a is simple, fast, and deterministic.

Conceptually:

```text
hash = FNV_OFFSET_BASIS

for every byte:
    hash ^= byte
    hash *= FNV_PRIME
```

For 64-bit FNV-1a:

```cpp
uint64_t hash = 14695981039346656037ULL;

for (unsigned char c : key)
{
    hash ^= c;
    hash *= 1099511628211ULL;
}
```

The hash is then used to determine the shard.

---

# 9. Important Hashing Property

A hash function does NOT decide ownership by understanding the meaning of a key.

For example:

```text
user:1
user:2
user:3
```

are treated as byte sequences.

The hash produces deterministic numeric values.

Then:

```text
hash(key) → shard ID
```

---

# 10. ShardManager

We will introduce a component responsible for shard selection.

Possible interface:

```cpp
class ShardManager
{
public:

    ShardManager(int shardCount);

    int getShard(const std::string& key) const;

private:

    int shardCount;
};
```

Later this component can become more sophisticated.

Its responsibility should remain focused:

> Given a key, determine which shard owns it.

---

# 11. Router Responsibility

The router should not store application data itself.

Its responsibility is:

```text
Client request
      ↓
Router
      ↓
calculate shard
      ↓
send request to shard
```

Example:

```text
SET user:42 hello

        ↓

hash("user:42")

        ↓

Shard 1

        ↓

Shard 1 primary
```

---

# 12. GET Routing

For:

```text
GET user:42
```

the router calculates:

```text
hash("user:42") % shardCount
```

Then sends the GET request to that shard.

The client should not need to know which shard contains the key.

---

# 13. SET Routing

For:

```text
SET user:42 hello
```

the same mapping is used:

```text
user:42
   ↓
hash
   ↓
Shard 1
   ↓
Shard 1 primary
   ↓
WAL
   ↓
replicas
```

The existing replication logic can remain inside the shard.

---

# 14. DEL Routing

DELETE follows exactly the same rule:

```text
DEL user:42
      ↓
hash(key)
      ↓
Shard 1
      ↓
Shard 1 primary
```

The router therefore does not need separate shard-selection logic for SET, GET, and DEL.

It only needs the key.

---

# 15. Proposed Architecture

The next architecture will look like:

```text
                         CLIENT
                           |
                           v
                    +-------------+
                    |    ROUTER   |
                    +-------------+
                           |
                 +---------+---------+
                 |                   |
                 v                   v
           +-----------+       +-----------+
           |  SHARD 0  |       |  SHARD 1  |
           +-----------+       +-----------+
                 |                   |
            Primary 0            Primary 1
                 |                   |
             +---+---+           +---+---+
             |       |           |       |
           B0-1    B0-2        B1-1    B1-2
```

Each shard becomes an independent replication group.

---

# 16. Important Design Principle

Do not mix shard selection with replication logic.

These are separate responsibilities:

```text
ShardManager
    ↓
Which shard?

ReplicationManager
    ↓
Which replica?

WAL
    ↓
What has been persisted?

KeyValueStore
    ↓
What data is stored?
```

This separation will make the project easier to extend and debug.

---

# 17. Initial Directory Structure

We will eventually add:

```text
include/
    shard_manager.h

src/
    shard_manager.cpp
```

The existing files remain:

```text
include/
    lru_cache.h
    wal.h
    replication.h

src/
    server.cpp
    replication.cpp
    wal.cpp
    lru_cache.cpp
```

Later, the server architecture will be refactored so each shard has its own storage/replication group.

---

# 18. Implementation Plan

## 14.1 — Sharding Concepts

Understand:

- Shard
- Partition
- Replication group
- Router
- Key ownership

Status:

```text
DONE
```

---

## 14.2 — Define Shard Ownership

Create the basic shard abstraction.

For example:

```cpp
struct Shard
{
    int id;
};
```

Initially:

```text
Shard 0
Shard 1
```

Status:

```text
NEXT
```

---

## 14.3 — Key → Shard Mapping

Implement:

```cpp
int getShard(const string& key);
```

Initially use deterministic hashing.

Status:

```text
NEXT
```

---

## 14.4 — FNV-1a

Implement the 64-bit FNV-1a function.

Test that:

```text
same key → same hash
same key → same shard
```

Status:

```text
NEXT
```

---

## 14.5 — ShardManager

Create:

```text
ShardManager
```

Responsibilities:

```text
key → hash → shard ID
```

No networking.

No WAL.

No replication.

Status:

```text
NEXT
```

---

## 14.6 — Route Operations

Modify the request flow:

```text
SET
GET
DEL
```

so that the key is first mapped to a shard.

Status:

```text
NEXT
```

---

## 14.7 — Multiple Shard Instances

Run more than one storage group.

For example:

```text
Shard 0 → ports 9001/9002/9003

Shard 1 → ports 9011/9012/9013
```

Status:

```text
NEXT
```

---

## 14.8 — Replication Per Shard

Each shard gets its own:

- WAL
- primary
- backups
- sequence numbers
- health state
- catch-up process

Status:

```text
NEXT
```

---

## 14.9 — Distribution Testing

Insert many keys:

```text
key1
key2
...
key10000
```

Count how many keys land in each shard.

Status:

```text
NEXT
```

---

## 14.10 — Failure Testing

Test:

```text
Shard 0 backup failure
Shard 1 backup failure
Shard 0 recovery
Shard 1 recovery
```

The failure of one shard's replica should not corrupt another shard.

---

# 19. What We Are NOT Doing Yet

We are intentionally not implementing:

- Consistent hashing
- Dynamic shard addition/removal
- Automatic rebalancing
- Distributed consensus
- Leader election
- Cross-shard transactions
- Two-phase commit
- Network partition consensus

Those are separate distributed-systems problems.

The first goal is a clean static sharding system.

---

# 20. Important Limitation of `hash % N`

With:

```text
shard = hash(key) % N
```

changing `N` changes ownership.

Example:

```text
N = 2
hash(key) % 2 = 0
```

After changing to:

```text
N = 3
hash(key) % 3 = 2
```

the key moves.

Therefore our initial implementation assumes a fixed number of shards.

Later, the project can introduce consistent hashing or another partitioning scheme.

---

# 21. End-to-End Request Flow

Eventually:

```text
Client
  |
  | SET user:42 hello
  v
Router
  |
  | hash("user:42")
  v
ShardManager
  |
  | Shard 1
  v
Shard 1 Primary
  |
  | WAL
  v
ReplicationManager
  |
  +------> Replica 1
  |
  +------> Replica 2
```

For GET:

```text
Client
  |
  | GET user:42
  v
Router
  |
  v
ShardManager
  |
  v
Shard 1
  |
  +--> Healthy replica
  |
  +--> Primary fallback
```

This reuses the replication work already completed.

---

# 22. Phase 14 Completion Criteria

Phase 14 is complete when:

```text
Multiple shards exist
        ↓
Keys map deterministically
        ↓
FNV-1a implemented
        ↓
SET routes correctly
        ↓
GET routes correctly
        ↓
DEL routes correctly
        ↓
Each shard has independent storage
        ↓
Each shard can replicate
        ↓
Shard failures are isolated
```

After Phase 14, the project will be significantly closer to a real distributed storage system.

---

# 23. Next Step

The first implementation task is:

## 14.2 — Define Shard Ownership

We will create the smallest possible shard abstraction first, without changing the working Phase 13 replication system.

Then we will build the `ShardManager` around it.
