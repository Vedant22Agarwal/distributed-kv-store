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

ShardManager shardManager(2);
ShardRouter shardRouter(shardManager);
KVRouter kvRouter(shardRouter);

constexpr int CONNECT_TIMEOUT_SECONDS = 2;
constexpr int CLIENT_TIMEOUT_SECONDS = 10;
constexpr int MAX_COMMAND_SIZE = 65536;

bool configureSocket(int sock, int timeoutSeconds)
{
    int noSigPipe = 1;

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

        if (n < 0 && errno == EINTR)
            continue;

        return false;
    }

    return true;
}

bool receiveLine(
    int sock,
    string& result)
{
    result.clear();

    char ch;

    while (true)
    {
        ssize_t n = recv(
            sock,
            &ch,
            1,
            0);

        if (n > 0)
        {
            if (ch == '\n')
                return true;

            if (ch != '\r')
                result += ch;

            if (result.size() > MAX_COMMAND_SIZE)
                return false;

            continue;
        }

        if (n < 0 && errno == EINTR)
            continue;

        return false;
    }
}

bool connectWithTimeout(
    int sock,
    const sockaddr_in& address,
    int timeoutSeconds)
{
    int originalFlags = fcntl(
        sock,
        F_GETFL,
        0);

    if (originalFlags < 0)
        return false;

    if (fcntl(
            sock,
            F_SETFL,
            originalFlags | O_NONBLOCK) < 0)
    {
        return false;
    }

    int result = connect(
        sock,
        reinterpret_cast<const sockaddr*>(&address),
        sizeof(address));

    if (result == 0)
    {
        fcntl(
            sock,
            F_SETFL,
            originalFlags);

        return true;
    }

    if (errno != EINPROGRESS)
    {
        fcntl(
            sock,
            F_SETFL,
            originalFlags);

        return false;
    }

    fd_set writeSet;
    FD_ZERO(&writeSet);
    FD_SET(sock, &writeSet);

    timeval timeout{};
    timeout.tv_sec = timeoutSeconds;
    timeout.tv_usec = 0;

    int selectResult = select(
        sock + 1,
        nullptr,
        &writeSet,
        nullptr,
        &timeout);

    if (selectResult <= 0)
    {
        fcntl(
            sock,
            F_SETFL,
            originalFlags);

        return false;
    }

    int socketError = 0;
    socklen_t errorLength = sizeof(socketError);

    if (getsockopt(
            sock,
            SOL_SOCKET,
            SO_ERROR,
            &socketError,
            &errorLength) < 0)
    {
        fcntl(
            sock,
            F_SETFL,
            originalFlags);

        return false;
    }

    if (socketError != 0)
    {
        fcntl(
            sock,
            F_SETFL,
            originalFlags);

        return false;
    }

    if (fcntl(
            sock,
            F_SETFL,
            originalFlags) < 0)
    {
        return false;
    }

    return true;
}

bool sendToEndpoint(
    const string& host,
    int port,
    const string& command,
    string& response)
{
    int sock = socket(
        AF_INET,
        SOCK_STREAM,
        0);

    if (sock == -1)
        return false;

    if (!configureSocket(
            sock,
            CLIENT_TIMEOUT_SECONDS))
    {
        close(sock);
        return false;
    }

    sockaddr_in address{};

    address.sin_family = AF_INET;
    address.sin_port = htons(port);

    if (inet_pton(
            AF_INET,
            host.c_str(),
            &address.sin_addr) <= 0)
    {
        close(sock);
        return false;
    }

    if (!connectWithTimeout(
            sock,
            address,
            CONNECT_TIMEOUT_SECONDS))
    {
        close(sock);
        return false;
    }

    if (!sendAll(
            sock,
            command + "\n"))
    {
        close(sock);
        return false;
    }

    string line;

    if (!receiveLine(
            sock,
            line))
    {
        close(sock);
        return false;
    }

    close(sock);

    response = line + "\n";

    return true;
}

string processRequest(
    const string& command)
{
    stringstream input(command);

    string operation;
    string key;

    if (!(input >> operation))
        return "ERROR: Empty command\n";

    if (operation == "PING")
        return "PONG\n";

    if (operation != "SET" &&
        operation != "GET" &&
        operation != "DEL")
    {
        return "ERROR: Unknown command\n";
    }

    if (!(input >> key))
        return "ERROR: Missing key\n";

    if (operation == "SET")
    {
        string value;

        getline(input, value);

        if (!value.empty() &&
            value[0] == ' ')
        {
            value.erase(0, 1);
        }

        if (value.empty())
            return "ERROR: Missing value\n";
    }

    int shardId;

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

    Shard shard =
        shardManager.getShardInfo(shardId);

    cout << operation
         << " key=" << key
         << " -> shard " << shardId
         << " primary "
         << shard.primaryHost << ":"
         << shard.primaryPort
         << endl;

    string response;

    /*
     * Always try the configured primary first.
     */
    if (sendToEndpoint(
            shard.primaryHost,
            shard.primaryPort,
            command,
            response))
    {
        return response;
    }

    cerr << "Primary unavailable for shard "
         << shardId
         << endl;

    /*
     * Writes are rejected while the primary
     * is unavailable.
     */
    if (operation == "SET" ||
        operation == "DEL")
    {
        return "ERROR: Primary unavailable; shard is read-only\n";
    }

    /*
     * GET only:
     *
     * Try backups directly.
     *
     * We do NOT send PING first because the GET
     * itself already tells us whether the backup
     * is reachable.
     */
    for (const auto& backup : shard.backups)
    {
        cout << "Trying read-only backup "
             << backup.first << ":"
             << backup.second
             << endl;

        if (sendToEndpoint(
                backup.first,
                backup.second,
                command,
                response))
        {
            cout << "Served GET from backup "
                 << backup.first << ":"
                 << backup.second
                 << endl;

            return response;
        }

        cerr << "Backup unavailable: "
             << backup.first << ":"
             << backup.second
             << endl;
    }

    return "ERROR: Primary and all backups unavailable\n";
}

void handleClient(
    int clientSocket)
{
    if (!configureSocket(
            clientSocket,
            CLIENT_TIMEOUT_SECONDS))
    {
        close(clientSocket);
        return;
    }

    string command;

    while (receiveLine(
        clientSocket,
        command))
    {
        string response =
            processRequest(command);

        if (!sendAll(
                clientSocket,
                response))
        {
            break;
        }
    }

    close(clientSocket);
}

int main()
{
    /*
     * --------------------------------------------------------
     * SHARD 0
     * --------------------------------------------------------
     */

    shardManager.setPrimary(
        0,
        "127.0.0.1",
        9001);

    shardManager.addBackup(
        0,
        "127.0.0.1",
        9002);

    shardManager.addBackup(
        0,
        "127.0.0.1",
        9003);

    /*
     * --------------------------------------------------------
     * SHARD 1
     * --------------------------------------------------------
     */

    shardManager.setPrimary(
        1,
        "127.0.0.1",
        9011);

    shardManager.addBackup(
        1,
        "127.0.0.1",
        9012);

    shardManager.addBackup(
        1,
        "127.0.0.1",
        9013);

    /*
     * --------------------------------------------------------
     * CREATE ROUTER SOCKET
     * --------------------------------------------------------
     */

    int serverSocket = socket(
        AF_INET,
        SOCK_STREAM,
        0);

    if (serverSocket == -1)
    {
        cerr << "Failed to create router socket\n";
        return 1;
    }

    int opt = 1;

    if (setsockopt(
            serverSocket,
            SOL_SOCKET,
            SO_REUSEADDR,
            &opt,
            sizeof(opt)) < 0)
    {
        cerr << "Failed to configure SO_REUSEADDR\n";
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
        cerr << "Failed to configure SO_NOSIGPIPE\n";
        close(serverSocket);
        return 1;
    }

    /*
     * --------------------------------------------------------
     * SERVER ADDRESS
     * --------------------------------------------------------
     */

    sockaddr_in address{};

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(8080);

    /*
     * --------------------------------------------------------
     * BIND
     * --------------------------------------------------------
     */

    if (::bind(
            serverSocket,
            reinterpret_cast<sockaddr*>(&address),
            sizeof(address)) < 0)
    {
        cerr << "Failed to bind port 8080\n";
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
            SOMAXCONN) < 0)
    {
        cerr << "Failed to listen\n";
        close(serverSocket);
        return 1;
    }

    cout << "KV Router listening on port 8080\n";

    /*
     * --------------------------------------------------------
     * ACCEPT CLIENTS
     * --------------------------------------------------------
     */

    while (true)
    {
        sockaddr_in clientAddress{};
        socklen_t clientLength =
            sizeof(clientAddress);

        int clientSocket = accept(
            serverSocket,
            reinterpret_cast<sockaddr*>(&clientAddress),
            &clientLength);

        if (clientSocket < 0)
        {
            if (errno == EINTR)
                continue;

            cerr << "Failed to accept client\n";
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