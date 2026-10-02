# Phase 7C: Integrating WAL with the TCP Server

## Objective

Connect the Write-Ahead Log (WAL) to the concurrent TCP server.

Previously, the server updated only the in-memory LRU cache:

```text
SET request
    |
    v
LRU cache update
    |
    v
Return success
```

After this phase:

```text
SET request
    |
    v
Validate command
    |
    v
Append SET record to WAL
    |
    v
Flush WAL
    |
    v
Update LRU cache
    |
    v
Return success
```

The same ordering applies to `DEL`.

## 1. Important Write-Ahead Rule

The server must not report a successful write if its WAL operation fails.

For `SET`:

```cpp
if (!wal.logSet(key, value))
{
    return "ERROR: WAL write failed\n";
}

wal.flush();
store.put(key, value);

return "ADDED\n";
```

For `DEL`:

```cpp
if (!wal.logDelete(key))
{
    return "ERROR: WAL write failed\n";
}

wal.flush();
store.remove(key);

return "DELETED\n";
```

This is the foundation of recovery.

> Note: `flush()` sends buffered data to the operating system. It does not yet provide a complete physical-storage durability guarantee. A later phase can add `fsync()`.

## 2. Global Objects

The server needs one shared WAL instance and one shared cache:

```cpp
LRUCache store(1000);
WAL wal("server.wal");
```

The WAL object is shared by client threads. Its internal mutex serializes WAL writes.

## 3. Update `src/tcp_server.cpp`

Use the following structure. Keep your existing socket setup and concurrent client handling, but replace the command-processing logic with this version.

```cpp
#include <arpa/inet.h>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

#include "../include/lru_cache.h"
#include "../include/wal.h"

using namespace std;

LRUCache store(1000);
WAL wal("server.wal");

string processCommand(const string& command)
{
    string inputLine = command;

    while (
        !inputLine.empty() &&
        (
            inputLine.back() == '\n' ||
            inputLine.back() == '\r'
        )
    )
    {
        inputLine.pop_back();
    }

    if (inputLine == "PING")
    {
        return "PONG\n";
    }

    stringstream input(inputLine);

    string operation;
    input >> operation;

    if (operation == "SET")
    {
        string key;
        input >> key;

        string value;
        getline(input, value);

        if (
            !value.empty() &&
            value[0] == ' '
        )
        {
            value.erase(0, 1);
        }

        if (
            key.empty() ||
            value.empty()
        )
        {
            return "ERROR: Invalid SET command\n";
        }

        /*
         * Write to WAL before changing the cache.
         */

        if (!wal.logSet(key, value))
        {
            return "ERROR: WAL write failed\n";
        }

        /*
         * Flush the WAL before reporting success.
         */

        wal.flush();

        /*
         * Only update the cache after the WAL write.
         */

        store.put(key, value);

        return "ADDED\n";
    }

    if (operation == "GET")
    {
        string key;
        input >> key;

        if (key.empty())
        {
            return "ERROR: Invalid GET command\n";
        }

        auto result = store.get(key);

        if (result.has_value())
        {
            return "VALUE: " + result.value() + "\n";
        }

        return "NOT_FOUND\n";
    }

    if (operation == "DEL")
    {
        string key;
        input >> key;

        if (key.empty())
        {
            return "ERROR: Invalid DEL command\n";
        }

        /*
         * Record the deletion before modifying the cache.
         */

        if (!wal.logDelete(key))
        {
            return "ERROR: WAL write failed\n";
        }

        wal.flush();

        store.remove(key);

        return "DELETED\n";
    }

    return "ERROR: Unknown command\n";
}

void handleClient(
    int clientSocket,
    sockaddr_in clientAddress
)
{
    char buffer[4096];

    string clientIP = inet_ntoa(
        clientAddress.sin_addr
    );

    cout << "Client connected: "
         << clientIP
         << endl;

    while (true)
    {
        memset(buffer, 0, sizeof(buffer));

        ssize_t bytesReceived = recv(
            clientSocket,
            buffer,
            sizeof(buffer) - 1,
            0
        );

        if (bytesReceived <= 0)
        {
            if (bytesReceived == 0)
            {
                cout << "Client closed connection: "
                     << clientIP
                     << endl;
            }
            else
            {
                perror("recv");
            }

            break;
        }

        string command(
            buffer,
            bytesReceived
        );

        string response = processCommand(command);

        ssize_t bytesSent = send(
            clientSocket,
            response.c_str(),
            response.size(),
            0
        );

        if (bytesSent < 0)
        {
            perror("send");
            break;
        }
    }

    close(clientSocket);

    cout << "Client disconnected: "
         << clientIP
         << endl;
}

int main()
{
    int serverSocket = socket(
        AF_INET,
        SOCK_STREAM,
        0
    );

    if (serverSocket < 0)
    {
        perror("socket");
        return 1;
    }

    int option = 1;

    if (
        setsockopt(
            serverSocket,
            SOL_SOCKET,
            SO_REUSEADDR,
            &option,
            sizeof(option)
        ) < 0
    )
    {
        perror("setsockopt");
        close(serverSocket);
        return 1;
    }

    sockaddr_in serverAddress{};

    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr = INADDR_ANY;
    serverAddress.sin_port = htons(8080);

    if (
        ::bind(
            serverSocket,
            reinterpret_cast<sockaddr*>(&serverAddress),
            sizeof(serverAddress)
        ) < 0
    )
    {
        perror("bind");
        close(serverSocket);
        return 1;
    }

    if (listen(serverSocket, 10) < 0)
    {
        perror("listen");
        close(serverSocket);
        return 1;
    }

    cout << "Server listening on port 8080\n";

    while (true)
    {
        sockaddr_in clientAddress{};
        socklen_t clientLength = sizeof(clientAddress);

        int clientSocket = accept(
            serverSocket,
            reinterpret_cast<sockaddr*>(&clientAddress),
            &clientLength
        );

        if (clientSocket < 0)
        {
            perror("accept");
            continue;
        }

        thread clientThread(
            handleClient,
            clientSocket,
            clientAddress
        );

        clientThread.detach();
    }

    close(serverSocket);

    return 0;
}
```

## 4. Compile

From the project root:

```bash
g++ -std=c++17 -Wall -Wextra -pedantic \
    -pthread \
    src/tcp_server.cpp \
    src/wal.cpp \
    -Iinclude \
    -o tcp_server
```

## 5. Start With a Clean WAL

Only do this during local testing:

```bash
rm -f server.wal
```

Start the server:

```bash
./tcp_server
```

Expected:

```text
Server listening on port 8080
```

## 6. Test Using the TCP Client

In another terminal:

```bash
./tcp_client
```

Run:

```text
SET name Vedant
```

Expected:

```text
ADDED
```

Then:

```text
GET name
```

Expected:

```text
VALUE: Vedant
```

Then:

```text
DEL name
```

Expected:

```text
DELETED
```

Finally:

```text
GET name
```

Expected:

```text
NOT_FOUND
```

## 7. Inspect the WAL

Stop the server with `Ctrl+C`.

Inspect the log:

```bash
cat server.wal
```

Expected:

```text
SET name Vedant
DEL name
```

The `GET` operation should not create a WAL record because it does not modify the store.

## 8. Restart-Recovery Test

The current server code above integrates WAL writing, but replay must also be called during startup to restore state.

Do not claim complete restart recovery until startup replay has been added and tested.

The intended future startup flow is:

```text
Create cache
    |
    v
Create WAL object
    |
    v
Replay WAL into cache
    |
    v
Start TCP listener
```

For this phase, verify that the server writes `SET` and `DEL` records correctly before adding startup replay.

## 9. Testing Checklist

- [x] Server compiles with `wal.cpp`.
- [x] Server starts on port 8080.
- [x] `SET` writes a WAL record.
- [x] `SET` updates the cache.
- [x] `GET` reads from the cache.
- [x] `DEL` writes a WAL record.
- [x] `DEL` removes the key.
- [x] `GET` does not write to the WAL.
- [x] WAL errors return an error response.
- [x] `server.wal` can be inspected manually.
- [x] Multiple clients can connect concurrently.

## 10. Current Limitations

### 10.1 Partial TCP messages

TCP is a byte stream. One `recv()` call does not necessarily correspond to one complete command.

The current implementation is suitable for basic local testing but needs proper newline-based command framing.

### 10.2 Partial sends

`send()` may send fewer bytes than requested. A production implementation should use a `sendAll()` helper.

### 10.3 Physical durability

`flush()` alone does not guarantee that data has reached stable storage.

### 10.4 Duplicate writes

If a client retries after a timeout, the same command may be logged more than once. Idempotency and request IDs will be addressed later.

### 10.5 Replay integration

The next server-startup change must replay the WAL before accepting requests.

## 11. Next Phase

The next step is to add startup replay:

```text
Server starts
    |
    v
Replay server.wal
    |
    v
Restore LRU cache
    |
    v
Start accepting clients
```

Then we will test:

1. Start server.
2. Set a key.
3. Stop server.
4. Start server again.
5. Get the same key.
6. Confirm that the value survived the restart.
