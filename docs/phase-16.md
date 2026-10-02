# Phase 16 — Stateless Router

## Objective

Implement and verify a stateless router that receives client requests, determines the correct shard using deterministic key-based routing, and forwards the request to the appropriate shard primary or backup.

The router must not store application key-value data, WAL data, or an LRU cache.

---

## Architecture

```text
Client
  |
  v
Router :8080
  |
  v
KVRouter
  |
  v
ShardRouter
  |
  v
ShardManager
  |
  +---- FNV-1a hash
  |
  +----> Shard 0 ----> Primary :9001
  |                    Backups :9002, :9003
  |
  +----> Shard 1 ----> Primary :9011
                       Backups :9012, :9013
```

### Router Responsibilities

* Receive client commands.
* Determine the shard from the key.
* Forward SET, GET, and DEL requests.
* Try the configured primary first.
* Reject SET/DEL when the primary is unavailable.
* Allow GET to fall back to configured backups.
* Handle multiple concurrent clients.

### Router Does Not Store

* Key-value data
* WAL records
* LRU cache
* Per-key state
* Application data

---

## Components Verified

### ShardManager

* Maintains shard topology.
* Stores primary and backup endpoints.
* Uses 64-bit FNV-1a hashing.
* Maps keys using:

```text
FNV-1a(key) % shardCount
```

### ShardRouter

Delegates key routing to `ShardManager`.

### KVRouter

Routes SET, GET, and DEL through the same deterministic shard mapping.

### Router Server

* Listens on port `8080`.
* Uses persistent client connections.
* Handles concurrent clients using separate threads.
* Uses socket timeouts.
* Handles interrupted system calls.
* Uses `SO_NOSIGPIPE`.

---

## Verification

### 1. Compilation

Router compiled successfully using:

```bash
g++ -std=c++17 -pthread \
src/router_server.cpp \
src/shard_manager.cpp \
src/shard_router.cpp \
src/kv_router.cpp \
-o router
```

**Result: PASSED**

---

### 2. Deterministic Routing

The following routing was observed:

```text
phase16 -> shard 1 -> 127.0.0.1:9011

key1 -> shard 1 -> 127.0.0.1:9011
key2 -> shard 0 -> 127.0.0.1:9001
key3 -> shard 1 -> 127.0.0.1:9011
key4 -> shard 0 -> 127.0.0.1:9001
```

The same key consistently mapped to the same shard for SET, GET, and DEL.

**Result: PASSED**

---

### 3. Cross-Shard Independence

Tested:

```text
SET key2 shard0_value
SET key1 shard1_value

GET key2
GET key1
```

Results:

```text
VALUE shard0_value
VALUE shard1_value
```

Then:

```text
DEL key2
GET key2
GET key1
```

Results:

```text
OK
NOT_FOUND
VALUE shard1_value
```

Deleting the key on Shard 0 did not affect the key on Shard 1.

**Result: PASSED**

---

### 4. Concurrent Router Test

Tested four concurrent clients.

Each client performed:

* 100 SET operations
* 100 GET operations

Total:

```text
4 clients
× 100 SET
× 100 GET
= 800 operations
```

Final result:

```text
CONCURRENT ROUTER TEST PASSED
```

**Result: PASSED**

---

## Phase 16 Completion Criteria

| Requirement                          | Status |
| ------------------------------------ | ------ |
| Stateless router architecture        | PASS   |
| Deterministic key → shard mapping    | PASS   |
| FNV-1a routing                       | PASS   |
| SET routing                          | PASS   |
| GET routing                          | PASS   |
| DEL routing                          | PASS   |
| Cross-shard isolation                | PASS   |
| Concurrent client handling           | PASS   |
| Router compilation                   | PASS   |
| No application data stored in router | PASS   |

---

## Conclusion

Phase 16 is **COMPLETE**.

The router successfully performs deterministic shard routing, forwards requests to the appropriate shard, maintains no application data, supports concurrent clients, and preserves isolation between shards.

No WAL files were cleared or deleted during Phase 16 testing.
