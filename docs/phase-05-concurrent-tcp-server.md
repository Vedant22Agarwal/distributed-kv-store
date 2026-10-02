# Phase 5: Concurrent TCP Server

## Objective

Upgrade the Phase 4 TCP server to support multiple clients concurrently.

- Accept multiple clients.
- Create one thread per client.
- Receive multiple messages from each client.
- Send responses independently.

## Main Changes

### 1. `handleClient()`

Client communication was moved into:

```cpp
void handleClient(int clientSocket, sockaddr_in clientAddress);
```

It receives messages, sends responses, logs the client IP, and closes that client's socket.

### 2. Continuous `accept()` loop

`accept()` now runs inside `while (true)`, allowing the server to accept new clients continuously.

### 3. One thread per client

```cpp
thread clientThread(
    handleClient,
    clientSocket,
    clientAddress
);

clientThread.detach();
```

The main thread continues accepting clients while worker threads handle communication.

### 4. `SO_REUSEADDR`

`setsockopt()` with `SO_REUSEADDR` makes it easier to restart the server on port `8080`.

## Compile and Run

```bash
g++ -std=c++17 -pthread src/tcp_server.cpp -o tcp_server
./tcp_server
```

## Testing

Open multiple terminals and run:

```bash
nc 127.0.0.1 8080
```

Each client should receive:

```text
Hello from server!
```

The server should remain active after a client disconnects.

## Architecture

```text
             Main Thread
                  |
             accept() loop
          /        |        \
     Thread 1   Thread 2   Thread 3
     Client 1   Client 2   Client 3
```

## Current Limitations

- Threads are detached.
- No graceful shutdown mechanism.
- No formal message protocol yet.
- Partial sends and message framing are not handled robustly.

These will be addressed in later phases.
