/**
 * @file server.cpp
 * @brief Implements the primary shard server for the key-value store.
 *
 * Handles client requests, WAL persistence, replication, and recovery.
 */

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <iostream>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <mutex>
#include <sstream>
#include <string>
#include <thread>

#include "../include/lru_cache.h"
#include "../include/replication.h"
#include "../include/wal.h"

#include <cstdlib>

using namespace std;

// In-memory cache used by the primary node.
LRUCache store(1000);

// WAL used to persist primary operations.
WAL wal(getenv("KV_WAL_PATH") ? getenv("KV_WAL_PATH") : "files/server.wal");

// Protects WAL and cache updates during write operations.
mutex storageMutex;

// Manages replication to configured backup nodes.
ReplicationManager replicationManager(&wal);

namespace
{
    constexpr int CLIENT_TIMEOUT_SECONDS = 10;
    constexpr int MAX_COMMAND_SIZE = 65536;

    // Configures socket timeouts and prevents SIGPIPE on macOS.
    bool configureClientSocket(int socket)
    {
        int noSigPipe = 1;

        if (setsockopt( socket, SOL_SOCKET, SO_NOSIGPIPE, &noSigPipe, sizeof(noSigPipe)) < 0)
        {
            return false;
        }

        timeval timeout{};
        timeout.tv_sec = CLIENT_TIMEOUT_SECONDS;
        timeout.tv_usec = 0;

        if (setsockopt(socket,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout)) < 0)
        {
            return false;
        }

        if (setsockopt(socket,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout)) < 0)
        {
            return false;
        }

        return true;
    }
}

// Sends the complete message even when send() writes only part of it.
bool sendAll(int socket,const string &message)
{
    size_t totalSent = 0;

    while (totalSent < message.size())
    {
        ssize_t bytesSent = send(
            socket,
            message.data() + totalSent,
            message.size() - totalSent,
            0);

        if (bytesSent > 0)
        {
            totalSent += static_cast<size_t>(bytesSent);
            continue;
        }

        if (bytesSent < 0 &&
            errno == EINTR)
        {
            continue;
        }

        return false;
    }

    return true;
}

// Parses and executes a single client command.
string processCommand(const string &command)
{
    string inputCommand = command;

    // Remove trailing newline characters from the received command.
    while (!inputCommand.empty() && (inputCommand.back() == '\n' || inputCommand.back() == '\r'))
    {
        inputCommand.pop_back();
    }

    if (inputCommand == "PING")
    {
        return "PONG\n";
    }

    if (inputCommand == "STATUS")
    {
        return "LAST_SEQUENCE " + to_string( wal.getLastSequenceNumber()) + "\n";
    }

    stringstream input(inputCommand);

    string operation;
    input >> operation;

    if (operation == "SET")
    {
        string key;
        input >> key;

        string value;
        getline(input, value);

        if (!value.empty() && value[0] == ' ')
        {
            value.erase(0, 1);
        }

        if (key.empty() || value.empty())
        {
            return "ERROR Invalid SET command\n";
        }

        // Serialize WAL, replication, and cache updates.
        lock_guard<mutex> lock(storageMutex);

        // Persist the operation before updating in-memory state.
        if (!wal.logSet(key, value))
        {
            return "ERROR: WAL write failed\n";
        }

        long long sequenceNumber = wal.getLastSequenceNumber();

        cout << "Primary WAL sequence: " << sequenceNumber << endl;

        // Replicate the operation to configured backups.
        bool replicationSuccessful = replicationManager.replicateSet( sequenceNumber, key, value);

        if (!replicationSuccessful)
        {
            cerr << "WARNING: SET replication incomplete" << endl;
        }

        // Update the primary cache after WAL persistence.
        store.put(key, value);

        return "ADDED\n";
    }

    if (operation == "GET")
    {
        string key;
        input >> key;

        if (key.empty())
        {
            return "ERROR Invalid GET command\n";
        }

        string value;

        // Prefer a healthy, up-to-date replica when available.
        if (replicationManager.getFromHealthyReplica(
                key,
                value))
        {
            if (value.empty())
                return "NOT_FOUND\n";

            return "VALUE: " +
                   value +
                   "\n";
        }

        // Fall back to the primary's local cache.
        auto result =
            store.get(key);

        if (result.has_value())
        {
            return "VALUE: " +
                   result.value() +
                   "\n";
        }

        return "NOT_FOUND\n";
    }

    if (operation == "DEL")
    {
        string key;
        input >> key;

        if (key.empty())
        {
            return "ERROR Invalid DEL command\n";
        }

        // Serialize WAL, replication, and cache updates.
        lock_guard<mutex> lock(storageMutex);

        if (!wal.logDelete(key))
        {
            return "ERROR: WAL write failed\n";
        }

        long long sequenceNumber =
            wal.getLastSequenceNumber();

        cout << "Primary WAL sequence: "
             << sequenceNumber
             << endl;

        // Replicate the delete operation to backups.
        bool replicationSuccessful =
            replicationManager.replicateDelete(
                sequenceNumber,
                key);

        if (!replicationSuccessful)
        {
            cerr << "WARNING: DEL replication incomplete"
                 << endl;
        }

        store.remove(key);

        return "DELETED\n";
    }

    return "ERROR: Unknown command\n";
}

// Handles all requests from one connected client.
void handleClient(
    int clientSocket,
    sockaddr_in clientAddress)
{
    char clientIp[INET_ADDRSTRLEN]{};

    const char *convertedIp =
        inet_ntop(
            AF_INET,
            &clientAddress.sin_addr,
            clientIp,
            sizeof(clientIp));

    if (convertedIp != nullptr)
    {
        cout << "Client connected: "
             << clientIp
             << endl;
    }

    if (!configureClientSocket(
            clientSocket))
    {
        cerr << "Failed to configure client socket for "
             << clientIp
             << endl;

        close(clientSocket);
        return;
    }

    char buffer[1024]{};

    // Keep the connection open for multiple requests.
    while (true)
    {
        ssize_t bytesReceived =
            recv(
                clientSocket,
                buffer,
                sizeof(buffer) - 1,
                0);

        if (bytesReceived > 0)
        {
            buffer[bytesReceived] = '\0';

            cout << "Received from "
                 << clientIp
                 << ": "
                 << buffer;

            const string response =
                processCommand(buffer);

            if (!sendAll(
                    clientSocket,
                    response))
            {
                cerr << "Send failed for client: "
                     << clientIp
                     << endl;

                break;
            }

            cout << "Response sent to "
                 << clientIp
                 << ": "
                 << response;

            continue;
        }

        if (bytesReceived == 0)
        {
            cout << "Client disconnected: "
                 << clientIp
                 << endl;

            break;
        }

        if (errno == EINTR)
        {
            continue;
        }

        cerr << "Receive failed for client: "
             << clientIp
             << " errno="
             << errno
             << endl;

        break;
    }

    close(clientSocket);

    cout << "Client handler stopped: "
         << clientIp
         << endl;
}

int main(
    int argc,
    char *argv[])
{
    int primaryPort = 9001;
    int backup1Port = 9002;
    int backup2Port = 9003;

    try
    {
        if (argc >= 2)
            primaryPort = stoi(argv[1]);

        if (argc >= 3)
            backup1Port = stoi(argv[2]);

        if (argc >= 4)
            backup2Port = stoi(argv[3]);
    }
    catch (const exception &e)
    {
        cerr << "Invalid port argument: "
             << e.what()
             << endl;

        return 1;
    }

    if (primaryPort < 1 ||
        primaryPort > 65535 ||
        backup1Port < 1 ||
        backup1Port > 65535 ||
        backup2Port < 1 ||
        backup2Port > 65535)
    {
        cerr << "Ports must be between 1 and 65535."
             << endl;

        return 1;
    }

    // Register the backups used for replication.
    replicationManager.addBackup(
        "127.0.0.1",
        backup1Port);

    replicationManager.addBackup(
        "127.0.0.1",
        backup2Port);

    cout << "Starting server..."
         << endl;

    // Recover the primary's in-memory state from the WAL.
    cout << "Replaying WAL..."
         << endl;

    if (!wal.replay(store))
    {
        cerr << "WAL replay failed. "
             << "Server shutting down."
             << endl;

        return 1;
    }

    cout << "WAL replay completed successfully."
         << endl;

    // Bring lagging backups up to the primary's WAL sequence.
    replicationManager.catchUpAllBackups();

    // Start background backup health monitoring.
    replicationManager.startHealthMonitoring();

    // Create the TCP listening socket.
    int serverSocket =
        socket(
            AF_INET,
            SOCK_STREAM,
            0);

    if (serverSocket == -1)
    {
        cerr << "Failed to create socket"
             << endl;

        return 1;
    }

    // Allow the server to reuse its port after restart.
    int reuse = 1;

    if (setsockopt(
            serverSocket,
            SOL_SOCKET,
            SO_REUSEADDR,
            &reuse,
            sizeof(reuse)) == -1)
    {
        cerr << "setsockopt failed"
             << endl;

        close(serverSocket);

        return 1;
    }

    // Configure the address used by the primary server.
    sockaddr_in serverAddress{};

    serverAddress.sin_family =
        AF_INET;

    serverAddress.sin_port =
        htons(primaryPort);

    serverAddress.sin_addr.s_addr =
        INADDR_ANY;

    // Bind the socket to the selected port.
    if (::bind(
            serverSocket,
            reinterpret_cast<sockaddr *>(
                &serverAddress),
            sizeof(serverAddress)) == -1)
    {
        cerr << "Bind failed"
             << endl;

        close(serverSocket);

        return 1;
    }

    // Start listening for client connections.
    if (listen(
            serverSocket,
            SOMAXCONN) == -1)
    {
        cerr << "Listen failed"
             << endl;

        close(serverSocket);

        return 1;
    }

    cout << "Shard primary listening on port "
         << primaryPort
         << "..."
         << endl;

    // Accept clients and handle each connection in its own thread.
    while (true)
    {
        sockaddr_in clientAddress{};

        socklen_t clientAddressLength =
            sizeof(clientAddress);

        int clientSocket =
            accept(
                serverSocket,
                reinterpret_cast<sockaddr *>(
                    &clientAddress),
                &clientAddressLength);

        if (clientSocket == -1)
        {
            if (errno == EINTR)
                continue;

            cerr << "Accept failed"
                 << endl;

            continue;
        }

        thread clientThread(
            handleClient,
            clientSocket,
            clientAddress);

        clientThread.detach();
    }

    close(serverSocket);

    return 0;
}
