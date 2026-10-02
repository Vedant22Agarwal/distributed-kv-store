# Phase 4: TCP Networking

**Project:** Distributed Sharded Key-Value Store\
**Phase:** 4\
**Technology:** C++ / TCP Sockets / Linux and macOS

------------------------------------------------------------------------

## 1. Objective

The objective of this phase is to understand and implement basic TCP
networking.

The server should be able to:

1.  Create a TCP socket.
2.  Bind the socket to an IP address and port.
3.  Listen for incoming connections.
4.  Accept a client connection.
5.  Receive data from the client.
6.  Send a response to the client.
7.  Handle multiple messages from one client.
8.  Detect client disconnection.
9.  Close sockets safely.

The next extension will support multiple concurrent clients.

------------------------------------------------------------------------

## 2. What Is TCP?

TCP stands for **Transmission Control Protocol**.

It is a connection-oriented protocol that provides:

-   Reliable data delivery.
-   Ordered transmission of bytes.
-   Error detection and retransmission.
-   Communication between applications over a network.

In this project, TCP will allow communication between:

``` text
Client <---- TCP Connection ----> KV Store Node
```

Later, TCP will also be used for communication between primary and
backup nodes.

------------------------------------------------------------------------

## 3. Basic TCP Server Flow

``` text
socket()
   |
   v
bind()
   |
   v
listen()
   |
   v
accept()
   |
   v
recv() / send()
   |
   v
close()
```

The current implementation accepts one client and handles multiple
messages from that client.

------------------------------------------------------------------------

## 4. File Structure

``` text
distributed-kv-store/
├── include/
│   └── lru_cache.h
├── src/
│   ├── main.cpp
│   ├── lru_cache.cpp
│   └── tcp_server.cpp
├── tests/
│   └── ...
├── docs/
│   ├── phase-01-basic-kv-store.md
│   ├── phase-02-lru-cache.md
│   ├── phase-03-thread-safety.md
│   └── phase-04-tcp-networking.md
├── CMakeLists.txt
└── README.md
```

------------------------------------------------------------------------

## 5. Required Header Files

``` cpp
#include <arpa/inet.h>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
```

  Header             Purpose
  ------------------ ---------------------------------
  `<arpa/inet.h>`    IP address conversion functions
  `<cstring>`        C-style string utilities
  `<iostream>`       Input and output
  `<netinet/in.h>`   IPv4 address structures
  `<sys/socket.h>`   Socket API
  `<unistd.h>`       `close()` and Unix system calls

------------------------------------------------------------------------

## 6. Creating a Socket

``` cpp
int serverSocket = socket(
    AF_INET,
    SOCK_STREAM,
    0
);
```

### Parameters

-   `AF_INET`: Uses IPv4 addressing.
-   `SOCK_STREAM`: Creates a stream socket, normally used with TCP.
-   `0`: Allows the operating system to select the appropriate protocol.

### Return value

-   `-1`: Socket creation failed.
-   Nonnegative integer: Socket file descriptor.

``` cpp
if (serverSocket == -1)
{
    cerr << "Failed to create socket\n";
    return 1;
}
```

A socket descriptor is an integer used by the process to identify an
open socket.

------------------------------------------------------------------------

## 7. Binding the Socket

``` cpp
sockaddr_in serverAddress{};

serverAddress.sin_family = AF_INET;
serverAddress.sin_port = htons(8080);
serverAddress.sin_addr.s_addr = INADDR_ANY;
```

### Explanation

#### `sockaddr_in`

Stores an IPv4 address and port.

#### `sin_family`

Specifies the address family:

``` cpp
AF_INET
```

#### `htons(8080)`

Converts the port number from host byte order to network byte order.

#### `INADDR_ANY`

Allows the server to listen on all local IPv4 interfaces.

For local testing, connect using:

``` text
127.0.0.1:8080
```

### Calling `bind()`

``` cpp
if (::bind(
        serverSocket,
        reinterpret_cast<sockaddr*>(&serverAddress),
        sizeof(serverAddress)
    ) == -1)
{
    cerr << "Bind failed\n";
    close(serverSocket);
    return 1;
}
```

### Why `::bind()`?

The C++ standard library contains `std::bind`. When using:

``` cpp
using namespace std;
```

there can be a naming conflict.

Using:

``` cpp
::bind()
```

explicitly selects the global socket function.

------------------------------------------------------------------------

## 8. Listening for Connections

``` cpp
if (listen(serverSocket, SOMAXCONN) == -1)
{
    cerr << "Listen failed\n";
    close(serverSocket);
    return 1;
}
```

`listen()` changes the socket into a listening socket.

`SOMAXCONN` requests the system's maximum supported connection backlog
value.

The success message should be printed after `listen()` succeeds:

``` cpp
cout << "Server listening on port 8080...\n";
```

------------------------------------------------------------------------

## 9. Accepting a Client

``` cpp
sockaddr_in clientAddress{};

socklen_t clientAddressLength =
    sizeof(clientAddress);

int clientSocket = accept(
    serverSocket,
    reinterpret_cast<sockaddr*>(&clientAddress),
    &clientAddressLength
);
```

### Two important sockets

  Socket           Purpose
  ---------------- ---------------------------------------
  `serverSocket`   Listens for new connections
  `clientSocket`   Communicates with the accepted client

`accept()` returns a new socket for the client. The listening socket
remains available for accepting additional clients, although the current
program calls `accept()` only once.

------------------------------------------------------------------------

## 10. Displaying the Client IP

``` cpp
char clientIp[INET_ADDRSTRLEN]{};

const char* convertedIp = inet_ntop(
    AF_INET,
    &clientAddress.sin_addr,
    clientIp,
    sizeof(clientIp)
);

if (convertedIp != nullptr)
{
    cout << "Client IP: "
         << clientIp << endl;
}
```

`inet_ntop()` converts a binary IPv4 address into readable text.

Example:

``` text
Binary IPv4 address -> 127.0.0.1
```

`INET_ADDRSTRLEN` provides the required buffer size for a textual IPv4
address, including its null terminator.

------------------------------------------------------------------------

## 11. Receiving Data

``` cpp
char buffer[1024]{};

ssize_t bytesReceived = recv(
    clientSocket,
    buffer,
    sizeof(buffer) - 1,
    0
);
```

### Parameters

  Parameter              Explanation
  ---------------------- ----------------------------------------
  `clientSocket`         Socket from which data is received
  `buffer`               Memory where received bytes are stored
  `sizeof(buffer) - 1`   Maximum number of bytes to receive
  `0`                    Default flags

### Return values

  Return value   Meaning
  -------------- ------------------------------
  Positive       Number of bytes received
  `0`            Client closed the connection
  `-1`           An error occurred

Because `recv()` returns bytes, the data is not automatically a C-style
string. We add a null terminator:

``` cpp
buffer[bytesReceived] = '\0';
```

### Important TCP concept

TCP is a byte stream. One `recv()` call does not necessarily represent
one complete application-level message.

A real application should define a message-framing protocol, such as:

-   Newline-delimited messages.
-   Length-prefixed messages.
-   Fixed-size messages.
-   A structured protocol.

------------------------------------------------------------------------

## 12. Sending a Response

``` cpp
const string response = "Hello from server!\n";

ssize_t bytesSent = send(
    clientSocket,
    response.c_str(),
    response.size(),
    0
);
```

### Parameters

  Parameter            Explanation
  -------------------- ---------------------------
  `clientSocket`       Destination socket
  `response.c_str()`   Pointer to response bytes
  `response.size()`    Number of bytes to send
  `0`                  Default flags

The return value indicates how many bytes were sent, or `-1` if an error
occurred.

### Production consideration

`send()` may send fewer bytes than requested. A production
implementation should use a helper that continues sending until the
entire response has been transmitted or an error occurs.

------------------------------------------------------------------------

## 13. Handling Multiple Messages from One Client

The current implementation uses a loop:

``` cpp
while (true)
{
    ssize_t bytesReceived = recv(
        clientSocket,
        buffer,
        sizeof(buffer) - 1,
        0
    );

    if (bytesReceived > 0)
    {
        buffer[bytesReceived] = '\0';

        cout << "Received: "
             << buffer << endl;

        const string response = "Hello from server!\n";

        ssize_t bytesSent = send(
            clientSocket,
            response.c_str(),
            response.size(),
            0
        );

        if (bytesSent == -1)
        {
            cerr << "Send failed\n";
            break;
        }

        cout << "Response sent: "
             << bytesSent << " bytes\n";
    }
    else if (bytesReceived == 0)
    {
        cout << "Client closed the connection.\n";
        break;
    }
    else
    {
        cerr << "Receive failed\n";
        break;
    }
}
```

### Behavior

``` text
Client connects
      |
      v
Receive message
      |
      v
Send response
      |
      v
Receive next message
      |
      v
Send response
      |
      v
Client disconnects
      |
      v
Exit loop
      |
      v
Close sockets
      |
      v
Server terminates
```

### Why `break` is important

When `recv()` returns `0`, the client has closed its connection:

``` cpp
else if (bytesReceived == 0)
{
    cout << "Client closed the connection.\n";
    break;
}
```

The `break` statement exits the receive loop. Execution then continues
with socket cleanup.

When `recv()` returns `-1`, the error branch should also use `break` so
that the server does not repeatedly print the same error.

------------------------------------------------------------------------

## 14. Closing Sockets

``` cpp
close(clientSocket);
close(serverSocket);

cout << "Server stopped.\n";
```

-   `clientSocket` represents the individual client connection.
-   `serverSocket` represents the listening socket.

The current program closes both sockets after the client disconnects.

------------------------------------------------------------------------

## 15. Testing

### Compile the server

``` bash
g++ src/tcp_server.cpp -o tcp_server
```

### Start the server

``` bash
./tcp_server
```

Expected output:

``` text
Server listening on port 8080...
```

### Connect using Netcat

Open a second terminal:

``` bash
nc 127.0.0.1 8080
```

Type:

``` text
Hello
```

Expected server output:

``` text
Client connected!
Client IP: 127.0.0.1
Received: Hello
Response sent: 19 bytes
```

The client terminal should receive:

``` text
Hello from server!
```

You can send multiple messages from the same Netcat session.

### Stop the server

Press:

``` text
Ctrl + C
```

------------------------------------------------------------------------

## 16. Port Already in Use

If you receive:

``` text
Bind failed
```

the port may already be occupied by another process.

Check port 8080:

``` bash
lsof -i :8080
```

Stop the process using its actual PID:

``` bash
kill <PID>
```

If necessary:

``` bash
kill -9 <PID>
```

Then start the server again:

``` bash
./tcp_server
```

Later, we can add `SO_REUSEADDR` to make restarting the server more
convenient.

------------------------------------------------------------------------

## 17. Testing Checklist

-   [x] Socket creation works.
-   [x] `bind()` works.
-   [x] `listen()` works.
-   [x] Client can connect.
-   [x] Client IP can be displayed.
-   [x] Server receives multiple messages from one client.
-   [x] Server sends responses.
-   [x] Client disconnection is detected.
-   [x] Receive and send failures exit the loop.
-   [ ] Multiple clients concurrently.
-   [ ] Graceful shutdown.
-   [ ] Robust partial-send handling.
-   [ ] Application-level message protocol.

------------------------------------------------------------------------

## 18. Current Limitations

The current implementation is intentionally simple.

1.  **Only one client:** `accept()` is called once.
2.  **Server termination:** The server closes after the client
    disconnects.
3.  **Blocking operations:** `accept()` and `recv()` block until an
    event occurs.
4.  **No concurrency:** Threads or asynchronous I/O are not implemented
    yet.
5.  **No application protocol:** Messages are plain text rather than
    structured KV commands.
6.  **No authentication:** Any client that can connect can send data.
7.  **Partial TCP messages:** TCP does not preserve application message
    boundaries.
8.  **Partial sends:** The code does not yet guarantee that every
    requested byte is sent.

------------------------------------------------------------------------

## 19. Connection to the KV Store

Eventually, the server will receive commands such as:

``` text
SET name Vedant
GET name
DELETE name
```

The expected flow will become:

``` text
Client
   |
   | TCP request
   v
KV Node
   |
   | Parse command
   v
Storage Engine
   |
   | Read / Write
   v
LRU Cache + Storage
   |
   v
TCP Response
```

In later phases, the same networking foundation will support
communication between primary and backup nodes.

------------------------------------------------------------------------

## 20. Next Step: Multiple Concurrent Clients

The next extension is to allow the server to accept multiple clients
while existing clients are being handled.

Conceptual structure:

``` cpp
while (true)
{
    int clientSocket = accept(
        serverSocket,
        nullptr,
        nullptr
    );

    thread(
        handleClient,
        clientSocket
    ).detach();
}
```

This is only an outline. The implementation will first move the
client-handling logic into a separate `handleClient()` function. Then
the server will use threads to handle multiple clients concurrently.

------------------------------------------------------------------------

## 21. Conclusion

In Phase 4, we built and tested a basic TCP server.

The implementation now understands:

-   Socket creation.
-   IP address and port binding.
-   Listening for connections.
-   Accepting clients.
-   Receiving and sending data.
-   Client IP conversion.
-   Multiple messages from one client.
-   Client disconnection.
-   Basic error handling.

The next step is to implement **multiple concurrent TCP clients using
C++ threads**.
