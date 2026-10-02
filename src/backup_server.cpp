#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <mutex>

#include "../include/lru_cache.h"
#include "../include/wal.h"

using namespace std;

LRUCache store(1000);
WAL* wal = nullptr;
mutex storageMutex;

namespace
{
constexpr int CLIENT_TIMEOUT_SECONDS = 10;
constexpr size_t MAX_PENDING_SIZE = 65536;

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

bool sendAll(
    int sock,
    const string& message)
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

        if (n < 0 &&
            errno == EINTR)
        {
            continue;
        }

        return false;
    }

    return true;
}

string processReplicationCommand(
    const string& command)
{
    stringstream input(command);
    string operation;

    if (!(input >> operation))
        return "ERROR: Empty command\n";

    if (operation == "PING")
        return "PONG\n";

    if (operation == "STATUS")
    {
        lock_guard<mutex> lock(storageMutex);

        return "LAST_SEQUENCE " +
               to_string(
                   wal->getLastSequenceNumber()) +
               "\n";
    }

    if (operation == "GET")
    {
        string key;

        if (!(input >> key))
            return "ERROR: Missing key\n";

        lock_guard<mutex> lock(storageMutex);

        auto result = store.get(key);

        if (result.has_value())
        {
            return "VALUE: " +
                   result.value() +
                   "\n";
        }

        return "NOT_FOUND\n";
    }

    if (operation == "SET")
    {
        string key;

        if (!(input >> key))
            return "ERROR: Missing key\n";

        string value;
        getline(input, value);

        if (!value.empty() &&
            value[0] == ' ')
        {
            value.erase(0, 1);
        }

        if (value.empty())
            return "ERROR: Missing value\n";

        lock_guard<mutex> lock(storageMutex);

        long long sequence =
            wal->getLastSequenceNumber() + 1;

        if (!wal->logSetWithSequence(
                sequence,
                key,
                value))
        {
            cerr << "SET WAL write failed at sequence "
                 << sequence
                 << endl;

            return "ERROR: WAL write failed\n";
        }

        store.put(key, value);

        return "ADDED\n";
    }

    if (operation == "DEL")
    {
        string key;

        if (!(input >> key))
            return "ERROR: Missing key\n";

        lock_guard<mutex> lock(storageMutex);

        auto result = store.get(key);

        if (!result.has_value())
            return "NOT_FOUND\n";

        long long sequence =
            wal->getLastSequenceNumber() + 1;

        if (!wal->logDeleteWithSequence(
                sequence,
                key))
        {
            cerr << "DEL WAL write failed at sequence "
                 << sequence
                 << endl;

            return "ERROR: WAL write failed\n";
        }

        store.remove(key);

        return "DELETED\n";
    }

    if (operation == "REPL_SET" ||
        operation == "REPL_DEL")
    {
        long long sequence;

        if (!(input >> sequence))
            return "ERROR: Invalid sequence\n";

        string key;

        if (!(input >> key))
            return "ERROR: Missing key\n";

        string value;

        if (operation == "REPL_SET")
        {
            getline(input, value);

            if (!value.empty() &&
                value[0] == ' ')
            {
                value.erase(0, 1);
            }

            if (value.empty())
                return "ERROR: Missing value\n";
        }

        lock_guard<mutex> lock(storageMutex);

        long long expected =
            wal->getLastSequenceNumber() + 1;

        if (sequence != expected)
        {
            cerr << "Replication sequence mismatch. Expected "
                 << expected
                 << ", received "
                 << sequence
                 << endl;

            return "ERROR_SEQUENCE\n";
        }

        bool success;

        if (operation == "REPL_SET")
        {
            success =
                wal->logSetWithSequence(
                    sequence,
                    key,
                    value);
        }
        else
        {
            success =
                wal->logDeleteWithSequence(
                    sequence,
                    key);
        }

        if (!success)
        {
            cerr << "Replication WAL write failed"
                 << endl;

            return "ERROR: WAL write failed\n";
        }

        if (operation == "REPL_SET")
        {
            store.put(key, value);
        }
        else
        {
            store.remove(key);
        }

        return "ACK\n";
    }

    return "ERROR: Unknown command\n";
}

void handleClient(
    int clientSocket)
{
    if (!configureClientSocket(
            clientSocket))
    {
        cerr << "Failed to configure backup client socket"
             << endl;

        close(clientSocket);
        return;
    }

    string pending;
    char buffer[4096];

    while (true)
    {
        ssize_t received =
            recv(
                clientSocket,
                buffer,
                sizeof(buffer),
                0);

        if (received > 0)
        {
            pending.append(
                buffer,
                static_cast<size_t>(received));

            if (pending.size() >
                MAX_PENDING_SIZE)
            {
                sendAll(
                    clientSocket,
                    "ERROR: Command too large\n");

                break;
            }

            size_t pos;

            while ((pos =
                        pending.find('\n'))
                   != string::npos)
            {
                string command =
                    pending.substr(
                        0,
                        pos);

                pending.erase(
                    0,
                    pos + 1);

                if (!command.empty() &&
                    command.back() == '\r')
                {
                    command.pop_back();
                }

                string response =
                    processReplicationCommand(
                        command);

                if (!sendAll(
                        clientSocket,
                        response))
                {
                    close(clientSocket);
                    return;
                }
            }

            continue;
        }

        if (received == 0)
        {
            break;
        }

        if (errno == EINTR)
        {
            continue;
        }

        break;
    }

    close(clientSocket);
}

int main(
    int argc,
    char* argv[])
{
    int port = 9002;

    try
    {
        if (argc >= 2)
            port = stoi(argv[1]);
    }
    catch (const exception& e)
    {
        cerr << "Invalid port: "
             << e.what()
             << endl;

        return 1;
    }

    if (port < 1 ||
        port > 65535)
    {
        cerr << "Port must be between 1 and 65535"
             << endl;

        return 1;
    }

    string walPath =
        "files/backup-" +
        to_string(port) +
        ".wal";

    WAL backupWal(walPath);

    wal = &backupWal;

    cout << "Starting backup on port "
         << port
         << endl;

    if (!wal->replay(store))
    {
        cerr << "WAL replay failed for "
             << walPath
             << endl;

        return 1;
    }

    int serverSocket =
        socket(
            AF_INET,
            SOCK_STREAM,
            0);

    if (serverSocket == -1)
    {
        cerr << "Socket creation failed\n";
        return 1;
    }

    int reuse = 1;

    if (setsockopt(
            serverSocket,
            SOL_SOCKET,
            SO_REUSEADDR,
            &reuse,
            sizeof(reuse)) < 0)
    {
        cerr << "SO_REUSEADDR failed\n";

        close(serverSocket);

        return 1;
    }

    int noSigPipe = 1;

    if (setsockopt(
            serverSocket,
            SOL_SOCKET,
            SO_NOSIGPIPE,
            &noSigPipe,
            sizeof(noSigPipe)) < 0)
    {
        cerr << "SO_NOSIGPIPE failed\n";

        close(serverSocket);

        return 1;
    }

    sockaddr_in address{};

    address.sin_family =
        AF_INET;

    address.sin_addr.s_addr =
        INADDR_ANY;

    address.sin_port =
        htons(port);

    if (::bind(
            serverSocket,
            reinterpret_cast<sockaddr*>(
                &address),
            sizeof(address)) == -1)
    {
        cerr << "Bind failed on port "
             << port
             << endl;

        close(serverSocket);

        return 1;
    }

    if (listen(
            serverSocket,
            SOMAXCONN) == -1)
    {
        cerr << "Listen failed\n";

        close(serverSocket);

        return 1;
    }

    cout << "Backup listening on port "
         << port
         << endl;

    while (true)
    {
        sockaddr_in clientAddress{};
        socklen_t length =
            sizeof(clientAddress);

        int clientSocket =
            accept(
                serverSocket,
                reinterpret_cast<sockaddr*>(
                    &clientAddress),
                &length);

        if (clientSocket == -1)
        {
            if (errno == EINTR)
                continue;

            cerr << "Accept failed"
                 << endl;

            continue;
        }

        thread(
            handleClient,
            clientSocket
        ).detach();
    }

    close(serverSocket);

    return 0;
}