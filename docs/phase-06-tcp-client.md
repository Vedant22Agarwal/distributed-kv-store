# Phase 6: TCP Client

## Objective

Create a C++ TCP client that connects to the key-value server, sends commands, receives responses, and closes the connection safely.

## Main Features

- Creates a TCP socket using `socket()`.
- Connects to `127.0.0.1:8080` using `connect()`.
- Reads input using `getline()`.
- Uses `sendAll()` to ensure the complete message is sent.
- Receives responses using `recv()`.
- Supports repeated request-response communication.
- Stops when the user types `exit`.
- Supports `SET`, `GET`, `DEL`, and `PING`.

## Communication Flow

```text
Client                         Server
  |                              |
  | -------- connect() --------> |
  |                              |
  | -------- send() -----------> |
  |                              |
  | <-------- recv() ----------- |
  |                              |
  | -------- send() -----------> |
  |                              |
  | <-------- recv() ----------- |
  |                              |
  | -------- close() ----------> |
```

## Why `sendAll()` Is Required

TCP is a byte-stream protocol. A call to `send()` may transmit fewer bytes than requested.

`sendAll()` repeatedly calls `send()` until the complete message is transmitted or an error occurs.

```cpp
bool sendAll(int socketFd, const string& message);
```

## Supported Commands

| Command | Example | Purpose |
|---|---|---|
| `SET` | `SET user:101:name Vedant` | Stores a value |
| `GET` | `GET user:101:name` | Retrieves a value |
| `DEL` | `DEL user:101:name` | Deletes a key |
| `PING` | `PING` | Checks basic server responsiveness |
| `exit` | `exit` | Closes the client |

## Compile and Run

Start the server:

```bash
./tcp_server
```

Compile the client:

```bash
g++ -std=c++17 src/tcp_client.cpp -o tcp_client
```

Run the client:

```bash
./tcp_client
```

## Example Session

```text
Enter message (type exit to quit): SET user:101:name Vedant
ADDED

Enter message (type exit to quit): GET user:101:name
VALUE: Vedant

Enter message (type exit to quit): PING
PONG

Enter message (type exit to quit): DEL user:101:name
DELETED

Enter message (type exit to quit): GET user:101:name
NOT_FOUND

Enter message (type exit to quit): exit

Client stopped.
```

## Current Limitations

- Newline-based message framing is not fully implemented.
- The server assumes one `recv()` contains one complete command.
- Responses are assumed to fit in one `recv()` call.
- The client connects to only one server.
- No authentication, authorization, request IDs, or sequence numbers exist.
- The client is not yet aware of shards or replica nodes.

## Learning Outcomes

After this phase, you should understand TCP socket creation, client-server communication, partial sends, receiving responses, and persistent client connections.

## Next Phase

### Phase 7: Write-Ahead Logging (WAL)

We will create a WAL file, log `SET` and `DEL` operations, flush entries, and replay the log after a restart to restore the key-value store.
