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

LRUCache store(1000);

WAL wal(
    getenv("KV_WAL_PATH")
        ? getenv("KV_WAL_PATH")
        : "files/server.wal");

mutex storageMutex;

ReplicationManager replicationManager(&wal);

namespace
{
constexpr int CLIENT_TIMEOUT_SECONDS = 10;
constexpr int MAX_COMMAND_SIZE = 65536;

bool configureClientSocket(int socket)
{
    int noSigPipe = 1;

    if (setsockopt(
            socket,
            SOL_SOCKET,
            SO_NOSIGPIPE,
            &noSigPipe,
            sizeof(noSigPipe)) < 0)
    {
        return false;
    }

    timeval timeout{};
    timeout.tv_sec = CLIENT_TIMEOUT_SECONDS;
    timeout.tv_usec = 0;

    if (setsockopt(
            socket,
            SOL_SOCKET,
            SO_RCVTIMEO,
            &timeout,
            sizeof(timeout)) < 0)
    {
        return false;
    }

    if (setsockopt(
            socket,
            SOL_SOCKET,
            SO_SNDTIMEO,
            &timeout,
            sizeof(timeout)) < 0)
    {
        return false;
    }

    return true;
}
}

/*
 * ============================================================
 * SEND ALL
 * ============================================================
 */

bool sendAll(
    int socket,
    const string &message)
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

/*
 * ============================================================
 * PROCESS COMMAND
 * ============================================================
 */

string processCommand(const string &command)
{
    string inputCommand = command;

    while (!inputCommand.empty() &&
           (inputCommand.back() == '\n' ||
            inputCommand.back() == '\r'))
    {
        inputCommand.pop_back();
    }

    if (inputCommand == "PING")
    {
        return "PONG\n";
    }

    if (inputCommand == "STATUS")
    {
        return "LAST_SEQUENCE " +
               to_string(
                   wal.getLastSequenceNumber()) +
               "\n";
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

        if (!value.empty() &&
            value[0] == ' ')
        {
            value.erase(0, 1);
        }

        if (key.empty() ||
            value.empty())
        {
            return "ERROR Invalid SET command\n";
        }

        lock_guard<mutex> lock(storageMutex);

        if (!wal.logSet(key, value))
        {
            return "ERROR: WAL write failed\n";
        }

        long long sequenceNumber =
            wal.getLastSequenceNumber();

        cout << "Primary WAL sequence: "
             << sequenceNumber
             << endl;

        bool replicationSuccessful =
            replicationManager.replicateSet(
                sequenceNumber,
                key,
                value);

        if (!replicationSuccessful)
        {
            cerr << "WARNING: SET replication incomplete"
                 << endl;
        }

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

/*
 * ============================================================
 * HANDLE CLIENT
 * ============================================================
 */

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

/*
 * ============================================================
 * MAIN
 * ============================================================
 */

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

    /*
     * --------------------------------------------------------
     * REGISTER BACKUPS
     * --------------------------------------------------------
     */

    replicationManager.addBackup(
        "127.0.0.1",
        backup1Port);

    replicationManager.addBackup(
        "127.0.0.1",
        backup2Port);

    /*
     * --------------------------------------------------------
     * RECOVER PRIMARY STATE
     * --------------------------------------------------------
     */

    cout << "Starting server..."
         << endl;

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

    /*
     * --------------------------------------------------------
     * CATCH UP BACKUPS
     * --------------------------------------------------------
     */

    replicationManager.catchUpAllBackups();

    replicationManager.startHealthMonitoring();

    /*
     * --------------------------------------------------------
     * CREATE SERVER SOCKET
     * --------------------------------------------------------
     */

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

    /*
     * --------------------------------------------------------
     * ALLOW PORT REUSE
     * --------------------------------------------------------
     */

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

    /*
     * --------------------------------------------------------
     * SERVER ADDRESS
     * --------------------------------------------------------
     */

    sockaddr_in serverAddress{};

    serverAddress.sin_family =
        AF_INET;

    serverAddress.sin_port =
        htons(primaryPort);

    serverAddress.sin_addr.s_addr =
        INADDR_ANY;

    /*
     * --------------------------------------------------------
     * BIND
     * --------------------------------------------------------
     */

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

    /*
     * --------------------------------------------------------
     * LISTEN
     * --------------------------------------------------------
     */

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

    /*
     * --------------------------------------------------------
     * ACCEPT CLIENTS
     * --------------------------------------------------------
     */

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