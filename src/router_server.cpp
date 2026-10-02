#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "../include/shard_manager.h"
#include "../include/shard_router.h"
#include "../include/kv_router.h"

using namespace std;

/**
 * @file router_server.cpp
 * @brief Implements the TCP router that receives client requests,
 *        determines the correct shard, and forwards requests to
 *        the primary or backup nodes.
 */

// Router manages two shards.
ShardManager shardManager(2);
ShardRouter shardRouter(shardManager);
KVRouter kvRouter(shardRouter);

constexpr int CONNECT_TIMEOUT_SECONDS = 2;
constexpr int CLIENT_TIMEOUT_SECONDS = 10;
constexpr int MAX_COMMAND_SIZE = 65536;

// Configures socket options such as timeouts and SIGPIPE handling.
bool configureSocket(int sock, int timeoutSeconds)
{
    int noSigPipe = 1;

    // Prevent SIGPIPE when the remote side disconnects.
    if (setsockopt(
            sock,
            SOL_SOCKET,
            SO_NOSIGPIPE,
            &noSigPipe,
            sizeof(noSigPipe)) < 0)
    {
        return false;
    }

    timeval timeout{};
    timeout.tv_sec = timeoutSeconds;
    timeout.tv_usec = 0;

    // Set receive and send timeouts.
    if (setsockopt(
            sock,
            SOL_SOCKET,
            SO_RCVTIMEO,
            &timeout,
            sizeof(timeout)) < 0)
    {
        return false;
    }

    if (setsockopt(
            sock,
            SOL_SOCKET,
            SO_SNDTIMEO,
            &timeout,
            sizeof(timeout)) < 0)
    {
        return false;
    }

    return true;
}

// Sends the complete message over TCP.
bool sendAll(int sock, const string &message)
{
    size_t sent = 0;

    while (sent < message.size())
    {
        ssize_t n = send(
            sock,
            message.data() + sent,
            message.size() - sent,
            0);

        if (n > 0)
        {
            sent += static_cast<size_t>(n);
            continue;
        }

        // Retry if send() was interrupted.
        if (n < 0 && errno == EINTR)
            continue;

        return false;
    }

    return true;
}

// Receives a newline-terminated message from the client.
bool receiveLine(int sock, string &result)
{
    result.clear();

    char ch;

    while (true)
    {
        ssize_t n = recv(sock, &ch, 1, 0);

        if (n > 0)
        {
            if (ch == '\n')
                return true;

            if (ch != '\r')
                result += ch;

            // Prevent excessively large commands.
            if (result.size() > MAX_COMMAND_SIZE)
                return false;

            continue;
        }

        // Retry if recv() was interrupted.
        if (n < 0 && errno == EINTR)
            continue;

        return false;
    }
}

// Establishes a TCP connection with a configurable timeout.
bool connectWithTimeout(int sock, const sockaddr_in &address, int timeoutSeconds)
{
    int originalFlags = fcntl(sock, F_GETFL, 0);

    if (originalFlags < 0)
        return false;

    // Use non-blocking mode while connecting.
    if (fcntl(
            sock,
            F_SETFL,
            originalFlags | O_NONBLOCK) < 0)
    {
        return false;
    }

    int result = connect(sock, reinterpret_cast<const sockaddr *>(&address), sizeof(address));

    if (result == 0)
    {
        fcntl(sock, F_SETFL, originalFlags);

        return true;
    }

    if (errno != EINPROGRESS)
    {
        fcntl(sock, F_SETFL, originalFlags);

        return false;
    }

    fd_set writeSet;
    FD_ZERO(&writeSet);
    FD_SET(sock, &writeSet);

    timeval timeout{};
    timeout.tv_sec = timeoutSeconds;
    timeout.tv_usec = 0;

    // Wait for the connection to complete or timeout.
    int selectResult = select(sock + 1, nullptr, &writeSet, nullptr, &timeout);

    if (selectResult <= 0)
    {
        fcntl(sock, F_SETFL, originalFlags);

        return false;
    }

    int socketError = 0;
    socklen_t errorLength = sizeof(socketError);

    // Check whether the connection actually succeeded.
    if (getsockopt(sock, SOL_SOCKET, SO_ERROR, &socketError, &errorLength) < 0)
    {
        fcntl(sock, F_SETFL, originalFlags);

        return false;
    }

    if (socketError != 0)
    {
        fcntl(sock, F_SETFL, originalFlags);

        return false;
    }

    // Restore the original socket flags.
    if (fcntl(sock, F_SETFL, originalFlags) < 0)
    {
        return false;
    }

    return true;
}

// Sends a request to a backend node and receives its response.
bool sendToEndpoint(const string &host, int port, const string &command, string &response)
{
    int sock = socket(AF_INET, SOCK_STREAM, 0);

    if (sock == -1)
        return false;

    if (!configureSocket(sock, CLIENT_TIMEOUT_SECONDS))
    {
        close(sock);
        return false;
    }

    sockaddr_in address{};

    address.sin_family = AF_INET;
    address.sin_port = htons(port);

    // Convert the backend IP address to binary form.
    if (inet_pton(AF_INET, host.c_str(), &address.sin_addr) <= 0)
    {
        close(sock);
        return false;
    }

    if (!connectWithTimeout(sock, address, CONNECT_TIMEOUT_SECONDS))
    {
        close(sock);
        return false;
    }

    // Send the request to the backend.
    if (!sendAll(sock, command + "\n"))
    {
        close(sock);
        return false;
    }

    string line;

    // Receive the backend response.
    if (!receiveLine(sock, line))
    {
        close(sock);
        return false;
    }

    close(sock);

    response = line + "\n";

    return true;
}

// Parses and routes a client request.
string processRequest(const string &command)
{
    stringstream input(command);

    string operation;
    string key;

    if (!(input >> operation))
        return "ERROR: Empty command\n";

    if (operation == "PING")
        return "PONG\n";

    if (operation != "SET" && operation != "GET" && operation != "DEL")
    {
        return "ERROR: Unknown command\n";
    }

    if (!(input >> key))
        return "ERROR: Missing key\n";

    if (operation == "SET")
    {
        string value;

        getline(input, value);

        if (!value.empty() && value[0] == ' ')
        {
            value.erase(0, 1);
        }

        if (value.empty())
            return "ERROR: Missing value\n";
    }

    int shardId;

    // Use the appropriate routing method for the operation.
    if (operation == "SET")
    {
        shardId = kvRouter.routeSet(key);
    }
    else if (operation == "GET")
    {
        shardId = kvRouter.routeGet(key);
    }
    else
    {
        shardId = kvRouter.routeDelete(key);
    }

    Shard shard = shardManager.getShardInfo(shardId);

    cout << operation << " key=" << key << " -> shard " << shardId
         << " primary " << shard.primaryHost << ":" << shard.primaryPort << endl;

    string response;

    // Always try the configured primary first.
    if (sendToEndpoint(shard.primaryHost, shard.primaryPort, command, response))
    {
        return response;
    }

    cerr << "Primary unavailable for shard " << shardId << endl;

    // Writes are rejected when the primary is unavailable.
    if (operation == "SET" || operation == "DEL")
    {
        return "ERROR: Primary unavailable; shard is read-only\n";
    }

    // GET requests can fall back to a backup node.
    for (const auto &backup : shard.backups)
    {
        cout << "Trying read-only backup " << backup.first << ":" << backup.second << endl;

        if (sendToEndpoint(backup.first, backup.second, command, response))
        {
            cout << "Served GET from backup " << backup.first << ":" << backup.second << endl;

            return response;
        }

        cerr << "Backup unavailable: " << backup.first << ":" << backup.second << endl;
    }

    return "ERROR: Primary and all backups unavailable\n";
}

// Handles a persistent TCP connection from a client.
void handleClient(int clientSocket)
{
    if (!configureSocket(clientSocket, CLIENT_TIMEOUT_SECONDS))
    {
        close(clientSocket);
        return;
    }

    string command;

    // Process multiple commands on the same client connection.
    while (receiveLine(clientSocket, command))
    {
        string response = processRequest(command);

        if (!sendAll(clientSocket, response))
        {
            break;
        }
    }

    close(clientSocket);
}

int main()
{
    // Configure primary and backup nodes for Shard 0.
    shardManager.setPrimary(0, "127.0.0.1", 9001);

    shardManager.addBackup(0, "127.0.0.1", 9002);

    shardManager.addBackup(0, "127.0.0.1", 9003);

    // Configure primary and backup nodes for Shard 1.
    shardManager.setPrimary(1, "127.0.0.1", 9011);

    shardManager.addBackup(1, "127.0.0.1", 9012);

    shardManager.addBackup(1, "127.0.0.1", 9013);

    // Create the router's TCP server socket.
    int serverSocket = socket(AF_INET, SOCK_STREAM, 0);

    if (serverSocket == -1)
    {
        cerr << "Failed to create router socket\n";
        return 1;
    }

    int opt = 1;

    // Allow the router to reuse the port after restarting.
    if (setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
    {
        cerr << "Failed to configure SO_REUSEADDR\n";
        close(serverSocket);
        return 1;
    }

    int noSigPipe = 1;

    // Prevent SIGPIPE when a client disconnects unexpectedly.
    if (setsockopt(serverSocket, SOL_SOCKET, SO_NOSIGPIPE, &noSigPipe, sizeof(noSigPipe)) < 0)
    {
        cerr << "Failed to configure SO_NOSIGPIPE\n";
        close(serverSocket);
        return 1;
    }

    // Configure the router to listen on port 8080.
    sockaddr_in address{};

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(8080);

    // Bind the router socket to port 8080.
    if (::bind(serverSocket, reinterpret_cast<sockaddr *>(&address), sizeof(address)) < 0)
    {
        cerr << "Failed to bind port 8080\n";
        close(serverSocket);
        return 1;
    }

    // Start listening for client connections.
    if (listen(serverSocket, SOMAXCONN) < 0)
    {
        cerr << "Failed to listen\n";
        close(serverSocket);
        return 1;
    }

    cout << "KV Router listening on port 8080\n";

    // Accept clients and handle each connection in a separate thread.
    while (true)
    {
        sockaddr_in clientAddress{};
        socklen_t clientLength = sizeof(clientAddress);

        int clientSocket = accept(serverSocket, reinterpret_cast<sockaddr *>(&clientAddress), &clientLength);

        if (clientSocket < 0)
        {
            if (errno == EINTR)
                continue;

            cerr << "Failed to accept client\n";
            continue;
        }

        thread(
            handleClient,
            clientSocket)
            .detach();
    }

    close(serverSocket);

    return 0;
}