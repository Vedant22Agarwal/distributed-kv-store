# How to Start the Distributed Key-Value Store

This guide explains how to build and start the complete **2-shard, 6-node distributed key-value store** and use the custom `tcp_client` to interact with it.

The final cluster contains:

```text
                         ┌─────────────────┐
                         │  tcp_client     │
                         └────────┬────────┘
                                  │
                                  ▼
                         ┌─────────────────┐
                         │  Router :8080   │
                         └────────┬────────┘
                                  │
                    ┌─────────────┴─────────────┐
                    │                           │
                    ▼                           ▼
             ┌──────────────┐            ┌──────────────┐
             │   Shard 0    │            │   Shard 1    │
             │   :9001      │            │   :9011      │
             │   Primary    │            │   Primary    │
             └──────┬───────┘            └──────┬───────┘
                    │                           │
              ┌─────┴─────┐               ┌─────┴─────┐
              ▼           ▼               ▼           ▼
           :9002       :9003           :9012       :9013
           Backup      Backup          Backup      Backup
```

---

# 1. Prerequisites

Make sure you have:

- C++17 compiler
- `g++`
- POSIX socket support
- macOS/Linux terminal
- `nc` is optional because the project includes its own `tcp_client`

Check:

```bash
g++ --version
```

---

# 2. Project Files

The important source files are:

```text
src/
├── server.cpp
├── backup_server.cpp
├── replication.cpp
├── replication.h
├── wal.cpp
├── wal.h
├── lru_cache.cpp
├── lru_cache.h
├── shard_manager.cpp
├── shard_manager.h
├── shard_router.cpp
├── shard_router.h
├── kv_router.cpp
├── kv_router.h
├── router_server.cpp
└── tcp_client.cpp
```

Persistent WAL files are stored under:

```text
files/
├── shard-0-primary.wal
├── shard-1-primary.wal
├── backup-9002.wal
├── backup-9003.wal
├── backup-9012.wal
└── backup-9013.wal
```

**Do not delete or clear the WAL files.**

---

# 3. Build the Project

Run these commands from the project root.

## Build Primary Node

```bash
g++ -std=c++17 -pthread \
src/server.cpp \
src/replication.cpp \
src/wal.cpp \
src/lru_cache.cpp \
-o primary_node
```

This creates:

```text
primary_node
```

---

## Build Backup Node

```bash
g++ -std=c++17 -pthread \
src/backup_server.cpp \
src/wal.cpp \
src/lru_cache.cpp \
-o backup_node
```

This creates:

```text
backup_node
```

The same executable is used for all four backup nodes.

---

## Build Router

```bash
g++ -std=c++17 -pthread \
src/router_server.cpp \
src/shard_manager.cpp \
src/shard_router.cpp \
src/kv_router.cpp \
-o router
```

This creates:

```text
router
```

---

## Build TCP Client

The project includes a custom TCP client that accepts the destination IP and port as command-line arguments.

Build it with:

```bash
g++ -std=c++17 src/tcp_client.cpp -o tcp_client
```

This creates:

```text
tcp_client
```

The client usage is:

```bash
./tcp_client <ip> <port>
```

For the normal application workflow, connect it to the router:

```bash
./tcp_client 127.0.0.1 8080
```

---

# 4. Start the Cluster

Start the nodes in this order:

```text
1. Backups
2. Primaries
3. Router
4. TCP Client
```

This makes sure the replica nodes are available when the primaries start.

---

# 5. Start Shard 0 Backup 1

Open **Terminal 1**:

```bash
./backup_node 9002
```

Expected:

```text
Backup server listening on port 9002
```

Keep this terminal running.

---

# 6. Start Shard 0 Backup 2

Open **Terminal 2**:

```bash
./backup_node 9003
```

Expected:

```text
Backup server listening on port 9003
```

Keep this terminal running.

---

# 7. Start Shard 1 Backup 1

Open **Terminal 3**:

```bash
./backup_node 9012
```

Expected:

```text
Backup server listening on port 9012
```

Keep this terminal running.

---

# 8. Start Shard 1 Backup 2

Open **Terminal 4**:

```bash
./backup_node 9013
```

Expected:

```text
Backup server listening on port 9013
```

At this point all four backup nodes should be running:

```text
9002  ✅
9003  ✅
9012  ✅
9013  ✅
```

---

# 9. Start Shard 0 Primary

Open **Terminal 5**:

```bash
KV_WAL_PATH=files/shard-0-primary.wal \
./primary_node 9001 9002 9003
```

This means:

```text
Primary port:
9001

Backups:
9002
9003

WAL:
files/shard-0-primary.wal
```

Expected:

```text
PRIMARY NODE listening on port 9001
```

Keep this terminal running.

---

# 10. Start Shard 1 Primary

Open **Terminal 6**:

```bash
KV_WAL_PATH=files/shard-1-primary.wal \
./primary_node 9011 9012 9013
```

This means:

```text
Primary port:
9011

Backups:
9012
9013

WAL:
files/shard-1-primary.wal
```

Expected:

```text
PRIMARY NODE listening on port 9011
```

Keep this terminal running.

---

# 11. Start the Router

Open **Terminal 7**:

```bash
./router
```

Expected:

```text
Router listening on port 8080
```

The router now knows:

```text
Shard 0:
Primary 9001
Backups 9002, 9003

Shard 1:
Primary 9011
Backups 9012, 9013
```

---

# 12. Start the TCP Client

Open **Terminal 8**:

```bash
./tcp_client 127.0.0.1 8080
```

The TCP client connects to the router.

You should see something similar to:

```text
========================================
       DISTRIBUTED KEY-VALUE STORE
========================================

Type 'exit' to quit

Available Commands:
  SET key value  - Store a value
  GET key        - Retrieve a value
  DEL key        - Delete a key
  PING           - Check server status
  exit           - Close connection
```

---

# 13. Test PING

Inside `tcp_client`, type:

```text
PING
```

Expected:

```text
PONG
```

This confirms:

```text
tcp_client
    ↓
router :8080
```

is working.

---

# 14. Test SET

Inside `tcp_client`:

```text
SET name Vedant
```

Expected:

```text
ADDED
```

The request goes:

```text
tcp_client
    ↓
Router :8080
    ↓
FNV-1a(key)
    ↓
Shard
    ↓
Primary
    ↓
WAL + LRU + Replication
```

---

# 15. Test GET

```text
GET name
```

Expected:

```text
VALUE: Vedant
```

---

# 16. Test DELETE

```text
DEL name
```

Expected:

```text
DELETED
```

Then:

```text
GET name
```

Expected:

```text
NOT_FOUND
```

---

# 17. Test Multiple Keys

Inside `tcp_client`:

```text
SET user1 Vedant
SET user2 IIT
SET city Dhanbad
SET language C++
```

Then:

```text
GET user1
GET user2
GET city
GET language
```

Each key is deterministically assigned to one of the two shards.

---

# 18. Test Shard Routing

The router terminal prints routing information.

For example:

```text
SET key=user1 -> shard 1 primary 127.0.0.1:9011
SET key=user2 -> shard 0 primary 127.0.0.1:9001
```

The exact shard depends on:

```text
FNV1a(key) % 2
```

Do not assume that a key containing `shard0` will actually go to Shard 0.

---

# 19. Test Replication Directly

You can also use `tcp_client` to connect directly to a node.

For example, connect to backup `9002`:

```bash
./tcp_client 127.0.0.1 9002
```

Then:

```text
GET user1
```

If `user1` belongs to Shard 0 and has been replicated there, the backup should return its value.

Similarly:

```bash
./tcp_client 127.0.0.1 9003
```

can be used to check the second Shard 0 backup.

For Shard 1:

```bash
./tcp_client 127.0.0.1 9012
```

and:

```bash
./tcp_client 127.0.0.1 9013
```

---

# 20. Recommended Normal Startup

Every time you want to run the complete project, use these terminals:

### Terminal 1

```bash
./backup_node 9002
```

### Terminal 2

```bash
./backup_node 9003
```

### Terminal 3

```bash
./backup_node 9012
```

### Terminal 4

```bash
./backup_node 9013
```

### Terminal 5

```bash
KV_WAL_PATH=files/shard-0-primary.wal \
./primary_node 9001 9002 9003
```

### Terminal 6

```bash
KV_WAL_PATH=files/shard-1-primary.wal \
./primary_node 9011 9012 9013
```

### Terminal 7

```bash
./router
```

### Terminal 8

```bash
./tcp_client 127.0.0.1 8080
```

---

# 21. One-Minute Smoke Test

After starting everything, run these commands inside `tcp_client`:

```text
PING
SET phase20_test final_value
GET phase20_test
DEL phase20_test
GET phase20_test
```

Expected:

```text
PONG
ADDED
VALUE: final_value
DELETED
NOT_FOUND
```

If all five responses are correct, the complete request path is working.

---

# 22. Failure Testing

The cluster can also be tested by stopping individual nodes with:

```text
Ctrl+C
```

For example, to test backup `9002`:

```text
1. Stop 9002
2. Continue using the router
3. Perform SET/GET operations
4. Verify 9003 still has replicated data
5. Restart 9002
6. Verify 9002 catches up
```

Restart:

```bash
./backup_node 9002
```

The same procedure applies to:

```text
9003
9012
9013
```

For a primary:

```text
9001
9011
```

a `GET` can fall back to a healthy replica, while writes require the primary.

---

# 23. Stopping the Cluster

When finished, stop the processes with:

```text
Ctrl+C
```

Recommended order:

```text
tcp_client
router
primaries
backups
```

You do not need to delete any WAL files.

---

# 24. Important Notes

### Do not clear WAL files

The following files contain persistent state:

```text
files/shard-0-primary.wal
files/shard-1-primary.wal
files/backup-9002.wal
files/backup-9003.wal
files/backup-9012.wal
files/backup-9013.wal
```

Do not run commands such as:

```bash
rm files/*.wal
```

unless you intentionally want to destroy the persisted state.

### Start backups before primaries

Recommended:

```text
Backups
   ↓
Primaries
   ↓
Router
   ↓
TCP Client
```

### Normal client connection

Use:

```bash
./tcp_client 127.0.0.1 8080
```

The router is the normal client-facing entry point.

---

# 25. Quick Reference

```text
BUILD
─────

Primary:
g++ -std=c++17 -pthread src/server.cpp src/replication.cpp src/wal.cpp src/lru_cache.cpp -o primary_node

Backup:
g++ -std=c++17 -pthread src/backup_server.cpp src/wal.cpp src/lru_cache.cpp -o backup_node

Router:
g++ -std=c++17 -pthread src/router_server.cpp src/shard_manager.cpp src/shard_router.cpp src/kv_router.cpp -o router

TCP Client:
g++ -std=c++17 src/tcp_client.cpp -o tcp_client
```

```text
START
─────

./backup_node 9002
./backup_node 9003
./backup_node 9012
./backup_node 9013

KV_WAL_PATH=files/shard-0-primary.wal ./primary_node 9001 9002 9003

KV_WAL_PATH=files/shard-1-primary.wal ./primary_node 9011 9012 9013

./router

./tcp_client 127.0.0.1 8080
```

```text
PORTS
─────

8080  Router
9001  Shard 0 Primary
9002  Shard 0 Backup
9003  Shard 0 Backup
9011  Shard 1 Primary
9012  Shard 1 Backup
9013  Shard 1 Backup
```

```text
CLIENT COMMANDS
───────────────

PING
SET <key> <value>
GET <key>
DEL <key>
exit
```

---

# 26. Final Startup Flow

```text
                 START
                   │
                   ▼
          Build all executables
                   │
                   ▼
          Start 4 backup nodes
                   │
                   ▼
          Start 2 primary nodes
                   │
                   ▼
              Start router
                   │
                   ▼
          Start tcp_client
                   │
                   ▼
              PING
                   │
                   ▼
          SET / GET / DEL
                   │
                   ▼
          Distributed KV Store
              is running
```

The normal entry point for application requests is:

```bash
./tcp_client 127.0.0.1 8080
```

The TCP client connects to the router; the router handles shard selection and forwards the request to the appropriate storage node.
