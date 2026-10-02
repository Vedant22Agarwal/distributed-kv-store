#include "../include/replication.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <iostream>
#include <netinet/in.h>
#include <sstream>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

using namespace std;

namespace
{
constexpr int SOCKET_TIMEOUT_SECONDS = 2;
constexpr int CONNECT_TIMEOUT_SECONDS = 2;
constexpr size_t MAX_RESPONSE_SIZE = 1024;

bool configureSocket(int socket)
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
    timeout.tv_sec = SOCKET_TIMEOUT_SECONDS;
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

bool connectWithTimeout(
    int socket,
    const sockaddr_in &address)
{
    int originalFlags = fcntl(
        socket,
        F_GETFL,
        0);

    if (originalFlags < 0)
        return false;

    if (fcntl(
            socket,
            F_SETFL,
            originalFlags | O_NONBLOCK) < 0)
    {
        return false;
    }

    int result = connect(
        socket,
        reinterpret_cast<const sockaddr *>(&address),
        sizeof(address));

    if (result == 0)
    {
        fcntl(
            socket,
            F_SETFL,
            originalFlags);

        return true;
    }

    if (errno != EINPROGRESS)
    {
        fcntl(
            socket,
            F_SETFL,
            originalFlags);

        return false;
    }

    fd_set writeSet;

    FD_ZERO(&writeSet);
    FD_SET(socket, &writeSet);

    timeval timeout{};
    timeout.tv_sec = CONNECT_TIMEOUT_SECONDS;
    timeout.tv_usec = 0;

    int selectResult = select(
        socket + 1,
        nullptr,
        &writeSet,
        nullptr,
        &timeout);

    if (selectResult <= 0)
    {
        fcntl(
            socket,
            F_SETFL,
            originalFlags);

        return false;
    }

    int socketError = 0;
    socklen_t errorLength =
        sizeof(socketError);

    if (getsockopt(
            socket,
            SOL_SOCKET,
            SO_ERROR,
            &socketError,
            &errorLength) < 0)
    {
        fcntl(
            socket,
            F_SETFL,
            originalFlags);

        return false;
    }

    if (socketError != 0)
    {
        fcntl(
            socket,
            F_SETFL,
            originalFlags);

        return false;
    }

    if (fcntl(
            socket,
            F_SETFL,
            originalFlags) < 0)
    {
        return false;
    }

    return true;
}
}

bool ReplicationManager::sendAll(
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

bool ReplicationManager::receiveLine(
    int socket,
    string &response)
{
    response.clear();

    char character;

    while (true)
    {
        ssize_t bytesReceived = recv(
            socket,
            &character,
            1,
            0);

        if (bytesReceived > 0)
        {
            if (character == '\n')
                return true;

            if (character != '\r')
                response += character;

            if (response.size() > MAX_RESPONSE_SIZE)
                return false;

            continue;
        }

        if (bytesReceived < 0 &&
            errno == EINTR)
        {
            continue;
        }

        return false;
    }
}

bool ReplicationManager::connectToBackup(
    const BackupNode &backup,
    int &socket)
{
    socket = ::socket(
        AF_INET,
        SOCK_STREAM,
        0);

    if (socket == -1)
        return false;

    if (!configureSocket(socket))
    {
        close(socket);
        socket = -1;
        return false;
    }

    sockaddr_in backupAddress{};

    backupAddress.sin_family = AF_INET;
    backupAddress.sin_port =
        htons(backup.port);

    if (inet_pton(
            AF_INET,
            backup.host.c_str(),
            &backupAddress.sin_addr) <= 0)
    {
        close(socket);
        socket = -1;
        return false;
    }

    if (!connectWithTimeout(
            socket,
            backupAddress))
    {
        close(socket);
        socket = -1;
        return false;
    }

    return true;
}

void ReplicationManager::addBackup(
    const string &host,
    int port)
{
    lock_guard<mutex> lock(replicationMutex);

    backups.push_back({
        host,
        port,
        0,
        ReplicaState::STARTING
    });
}

bool ReplicationManager::getBackupStatus(
    BackupNode &backup)
{
    int socket = -1;

    if (!connectToBackup(
            backup,
            socket))
    {
        setReplicaState(
            backup,
            ReplicaState::DOWN);

        cerr << "Backup unavailable: "
             << backup.host << ":"
             << backup.port
             << endl;

        return false;
    }

    if (backup.state ==
        ReplicaState::DOWN)
    {
        setReplicaState(
            backup,
            ReplicaState::STARTING);
    }

    if (!sendAll(
            socket,
            "STATUS\n"))
    {
        setReplicaState(
            backup,
            ReplicaState::DOWN);

        cerr << "Failed to send STATUS to backup: "
             << backup.host << ":"
             << backup.port
             << endl;

        close(socket);

        return false;
    }

    string response;

    if (!receiveLine(
            socket,
            response))
    {
        setReplicaState(
            backup,
            ReplicaState::DOWN);

        cerr << "Failed to receive STATUS response from backup: "
             << backup.host << ":"
             << backup.port
             << endl;

        close(socket);

        return false;
    }

    close(socket);

    string prefix;
    long long sequenceNumber;

    stringstream input(response);

    if (!(input >> prefix >> sequenceNumber))
    {
        setReplicaState(
            backup,
            ReplicaState::DOWN);

        cerr << "Invalid STATUS response from backup: "
             << response
             << endl;

        return false;
    }

    if (prefix != "LAST_SEQUENCE")
    {
        setReplicaState(
            backup,
            ReplicaState::DOWN);

        cerr << "Unexpected STATUS response from backup: "
             << response
             << endl;

        return false;
    }

    backup.lastSequenceNumber =
        sequenceNumber;

    long long primarySequence =
        primaryWAL->getLastSequenceNumber();

    if (sequenceNumber == primarySequence)
    {
        setReplicaState(
            backup,
            ReplicaState::HEALTHY);
    }
    else if (sequenceNumber < primarySequence)
    {
        setReplicaState(
            backup,
            ReplicaState::LAGGING);
    }
    else
    {
        setReplicaState(
            backup,
            ReplicaState::DOWN);
    }

    cout << "Backup "
         << backup.host << ":"
         << backup.port
         << " is at sequence "
         << backup.lastSequenceNumber
         << endl;

    return true;
}

bool ReplicationManager::replicateSet(
    long long sequenceNumber,
    const string &key,
    const string &value)
{
    lock_guard<mutex> lock(replicationMutex);

    bool allSuccessful = true;

    stringstream message;

    message << "REPL_SET "
            << sequenceNumber
            << " "
            << key
            << " "
            << value
            << "\n";

    string replicationMessage =
        message.str();

    for (auto &backup : backups)
    {
        int socket = -1;

        if (!connectToBackup(
                backup,
                socket))
        {
            setReplicaState(
                backup,
                ReplicaState::DOWN);

            cerr << "Backup unavailable: "
                 << backup.host << ":"
                 << backup.port
                 << endl;

            allSuccessful = false;

            continue;
        }

        if (!sendAll(
                socket,
                replicationMessage))
        {
            setReplicaState(
                backup,
                ReplicaState::DOWN);

            cerr << "Failed to send REPL_SET to backup: "
                 << backup.host << ":"
                 << backup.port
                 << endl;

            close(socket);

            allSuccessful = false;

            continue;
        }

        string response;

        if (!receiveLine(
                socket,
                response))
        {
            setReplicaState(
                backup,
                ReplicaState::DOWN);

            cerr << "Failed to receive ACK from backup: "
                 << backup.host << ":"
                 << backup.port
                 << endl;

            close(socket);

            allSuccessful = false;

            continue;
        }

        close(socket);

        if (response != "ACK")
        {
            setReplicaState(
                backup,
                ReplicaState::DOWN);

            cerr << "Unexpected replication response "
                 << "from backup "
                 << backup.host << ":"
                 << backup.port
                 << " -> "
                 << response
                 << endl;

            allSuccessful = false;

            continue;
        }

        backup.lastSequenceNumber =
            sequenceNumber;

        setReplicaState(
            backup,
            ReplicaState::HEALTHY);
    }

    return allSuccessful;
}

bool ReplicationManager::replicateDelete(
    long long sequenceNumber,
    const string &key)
{
    lock_guard<mutex> lock(replicationMutex);

    bool allSuccessful = true;

    stringstream message;

    message << "REPL_DEL "
            << sequenceNumber
            << " "
            << key
            << "\n";

    string replicationMessage =
        message.str();

    for (auto &backup : backups)
    {
        int socket = -1;

        if (!connectToBackup(
                backup,
                socket))
        {
            setReplicaState(
                backup,
                ReplicaState::DOWN);

            cerr << "Backup unavailable: "
                 << backup.host << ":"
                 << backup.port
                 << endl;

            allSuccessful = false;

            continue;
        }

        if (!sendAll(
                socket,
                replicationMessage))
        {
            setReplicaState(
                backup,
                ReplicaState::DOWN);

            cerr << "Failed to send REPL_DEL to backup: "
                 << backup.host << ":"
                 << backup.port
                 << endl;

            close(socket);

            allSuccessful = false;

            continue;
        }

        string response;

        if (!receiveLine(
                socket,
                response))
        {
            setReplicaState(
                backup,
                ReplicaState::DOWN);

            cerr << "Failed to receive ACK from backup: "
                 << backup.host << ":"
                 << backup.port
                 << endl;

            close(socket);

            allSuccessful = false;

            continue;
        }

        close(socket);

        if (response != "ACK")
        {
            setReplicaState(
                backup,
                ReplicaState::DOWN);

            cerr << "Unexpected replication response "
                 << "from backup "
                 << backup.host << ":"
                 << backup.port
                 << " -> "
                 << response
                 << endl;

            allSuccessful = false;

            continue;
        }

        backup.lastSequenceNumber =
            sequenceNumber;

        setReplicaState(
            backup,
            ReplicaState::HEALTHY);
    }

    return allSuccessful;
}

bool ReplicationManager::catchUpBackup(
    BackupNode &backup)
{
    if (!getBackupStatus(backup))
    {
        cerr << "Cannot catch up backup: "
             << backup.host << ":"
             << backup.port
             << endl;

        return false;
    }

    long long primarySequence =
        primaryWAL->getLastSequenceNumber();

    if (backup.lastSequenceNumber >=
        primarySequence)
    {
        if (backup.lastSequenceNumber ==
            primarySequence)
        {
            setReplicaState(
                backup,
                ReplicaState::HEALTHY);

            cout << "Backup "
                 << backup.host << ":"
                 << backup.port
                 << " is already synchronized."
                 << endl;

            return true;
        }

        setReplicaState(
            backup,
            ReplicaState::DOWN);

        cerr << "Invalid backup sequence. "
             << "Backup = "
             << backup.lastSequenceNumber
             << ", Primary = "
             << primarySequence
             << endl;

        return false;
    }

    setReplicaState(
        backup,
        ReplicaState::LAGGING);

    long long startSequence =
        backup.lastSequenceNumber + 1;

    cout << "Backup "
         << backup.host << ":"
         << backup.port
         << " needs catch-up from sequence "
         << startSequence
         << " to "
         << primarySequence
         << endl;

    vector<WALRecord> records =
        primaryWAL->getRecordsFromSequence(
            startSequence);

    if (records.empty())
    {
        setReplicaState(
            backup,
            ReplicaState::LAGGING);

        cerr << "No WAL records found for catch-up."
             << " Backup = "
             << backup.lastSequenceNumber
             << ", Primary = "
             << primarySequence
             << endl;

        return false;
    }

    int socket = -1;

    if (!connectToBackup(
            backup,
            socket))
    {
        setReplicaState(
            backup,
            ReplicaState::DOWN);

        cerr << "Unable to connect to backup "
             << "for catch-up: "
             << backup.host << ":"
             << backup.port
             << endl;

        return false;
    }

    for (const auto &record : records)
    {
        stringstream message;

        if (record.operation == "SET")
        {
            message << "REPL_SET "
                    << record.sequenceNumber
                    << " "
                    << record.key
                    << " "
                    << record.value
                    << "\n";
        }
        else if (record.operation == "DEL")
        {
            message << "REPL_DEL "
                    << record.sequenceNumber
                    << " "
                    << record.key
                    << "\n";
        }
        else
        {
            setReplicaState(
                backup,
                ReplicaState::LAGGING);

            cerr << "Unknown WAL operation: "
                 << record.operation
                 << endl;

            close(socket);

            return false;
        }

        if (!sendAll(
                socket,
                message.str()))
        {
            setReplicaState(
                backup,
                ReplicaState::DOWN);

            cerr << "Failed to send catch-up record "
                 << record.sequenceNumber
                 << " to backup "
                 << backup.host << ":"
                 << backup.port
                 << endl;

            close(socket);

            return false;
        }

        string response;

        if (!receiveLine(
                socket,
                response))
        {
            setReplicaState(
                backup,
                ReplicaState::DOWN);

            cerr << "No response for catch-up record "
                 << record.sequenceNumber
                 << " from backup "
                 << backup.host << ":"
                 << backup.port
                 << endl;

            close(socket);

            return false;
        }

        if (response != "ACK")
        {
            setReplicaState(
                backup,
                ReplicaState::LAGGING);

            cerr << "Backup rejected catch-up record "
                 << record.sequenceNumber
                 << ": "
                 << response
                 << endl;

            close(socket);

            return false;
        }

        cout << "Catch-up record "
             << record.sequenceNumber
             << " acknowledged."
             << endl;
    }

    close(socket);

    if (!getBackupStatus(backup))
    {
        cerr << "Could not verify backup after "
             << "catch-up."
             << endl;

        return false;
    }

    primarySequence =
        primaryWAL->getLastSequenceNumber();

    if (backup.lastSequenceNumber !=
        primarySequence)
    {
        setReplicaState(
            backup,
            ReplicaState::LAGGING);

        cerr << "Catch-up incomplete. "
             << "Primary = "
             << primarySequence
             << ", Backup = "
             << backup.lastSequenceNumber
             << endl;

        return false;
    }

    setReplicaState(
        backup,
        ReplicaState::HEALTHY);

    cout << "Backup "
         << backup.host << ":"
         << backup.port
         << " successfully caught up to sequence "
         << backup.lastSequenceNumber
         << endl;

    return true;
}

void ReplicationManager::catchUpAllBackups()
{
    lock_guard<mutex> lock(replicationMutex);

    for (auto &backup : backups)
    {
        cout << "\nChecking catch-up for backup "
             << backup.host << ":"
             << backup.port
             << endl;

        if (!catchUpBackup(backup))
        {
            cerr << "Catch-up failed for backup: "
                 << backup.host << ":"
                 << backup.port
                 << endl;
        }
    }
}

bool ReplicationManager::getFromHealthyReplica(
    const string &key,
    string &value)
{
    lock_guard<mutex> lock(replicationMutex);

    long long primarySequence =
        primaryWAL->getLastSequenceNumber();

    for (auto &backup : backups)
    {
        if (backup.state !=
            ReplicaState::HEALTHY)
        {
            continue;
        }

        if (backup.lastSequenceNumber !=
            primarySequence)
        {
            continue;
        }

        int socket = -1;

        if (!connectToBackup(
                backup,
                socket))
        {
            setReplicaState(
                backup,
                ReplicaState::DOWN);

            cerr << "Replica "
                 << backup.host << ":"
                 << backup.port
                 << " failed during GET."
                 << endl;

            continue;
        }

        stringstream message;

        message << "GET "
                << key
                << "\n";

        if (!sendAll(
                socket,
                message.str()))
        {
            setReplicaState(
                backup,
                ReplicaState::DOWN);

            cerr << "Failed to send GET to replica "
                 << backup.host << ":"
                 << backup.port
                 << endl;

            close(socket);

            continue;
        }

        string response;

        if (!receiveLine(
                socket,
                response))
        {
            setReplicaState(
                backup,
                ReplicaState::DOWN);

            cerr << "Failed to receive GET response "
                 << "from replica "
                 << backup.host << ":"
                 << backup.port
                 << endl;

            close(socket);

            continue;
        }

        close(socket);

        long long currentPrimarySequence =
            primaryWAL->getLastSequenceNumber();

        if (currentPrimarySequence !=
            primarySequence)
        {
            cout << "Replica "
                 << backup.host << ":"
                 << backup.port
                 << " read rejected because "
                 << "primary sequence changed from "
                 << primarySequence
                 << " to "
                 << currentPrimarySequence
                 << endl;

            continue;
        }

        if (response.rfind("VALUE: ", 0) == 0)
        {
            value = response.substr(7);

            cout << "GET served by healthy replica "
                 << backup.host << ":"
                 << backup.port
                 << endl;

            return true;
        }

        if (response == "NOT_FOUND")
        {
            value = "";

            cout << "GET served by healthy replica "
                 << backup.host << ":"
                 << backup.port
                 << endl;

            return true;
        }

        setReplicaState(
            backup,
            ReplicaState::DOWN);

        cerr << "Invalid GET response from replica "
             << backup.host << ":"
             << backup.port
             << " -> "
             << response
             << endl;
    }

    return false;
}

void ReplicationManager::startHealthMonitoring()
{
    if (healthMonitoring)
        return;

    healthMonitoring = true;

    healthThread =
        thread([this]()
               {
        while (healthMonitoring)
        {
            for (size_t i = 0; ; i++)
            {
                BackupNode backupCopy;
                ReplicaState originalState;
                long long originalSequence;

                {
                    lock_guard<mutex> lock(replicationMutex);

                    if (i >= backups.size())
                        break;

                    backupCopy = backups[i];

                    originalState =
                        backups[i].state;

                    originalSequence =
                        backups[i].lastSequenceNumber;
                }

                getBackupStatus(backupCopy);

                if (backupCopy.state ==
                    ReplicaState::LAGGING)
                {
                    cout << "Backup "
                         << backupCopy.host << ":"
                         << backupCopy.port
                         << " is LAGGING. "
                         << "Starting catch-up..."
                         << endl;

                    if (catchUpBackup(backupCopy))
                    {
                        cout << "Backup "
                             << backupCopy.host << ":"
                             << backupCopy.port
                             << " catch-up completed."
                             << endl;
                    }
                    else
                    {
                        cout << "Backup "
                             << backupCopy.host << ":"
                             << backupCopy.port
                             << " catch-up failed."
                             << endl;
                    }
                }

                {
                    lock_guard<mutex> lock(replicationMutex);

                    if (i >= backups.size())
                        continue;

                    BackupNode &actualBackup =
                        backups[i];

                    if (actualBackup.state !=
                            originalState ||
                        actualBackup.lastSequenceNumber !=
                            originalSequence)
                    {
                        continue;
                    }

                    actualBackup.lastSequenceNumber =
                        backupCopy.lastSequenceNumber;

                    setReplicaState(
                        actualBackup,
                        backupCopy.state);
                }
            }

            int interval =
                healthCheckIntervalSeconds.load();

            this_thread::sleep_for(
                chrono::seconds(interval));
        } });
}

void ReplicationManager::stopHealthMonitoring()
{
    healthMonitoring = false;

    if (healthThread.joinable())
        healthThread.join();
}

ReplicationManager::~ReplicationManager()
{
    stopHealthMonitoring();
}

void ReplicationManager::setHealthCheckInterval(
    int seconds)
{
    if (seconds <= 0)
    {
        throw invalid_argument(
            "Health check interval must be greater than 0");
    }

    healthCheckIntervalSeconds =
        seconds;
}

void ReplicationManager::setReplicaState(
    BackupNode &backup,
    ReplicaState newState)
{
    if (backup.state == newState)
        return;

    backup.state = newState;

    cout << "Backup "
         << backup.host << ":"
         << backup.port
         << " state changed to ";

    switch (newState)
    {
    case ReplicaState::STARTING:
        cout << "STARTING";
        break;

    case ReplicaState::HEALTHY:
        cout << "HEALTHY";
        break;

    case ReplicaState::LAGGING:
        cout << "LAGGING";
        break;

    case ReplicaState::DOWN:
        cout << "DOWN";
        break;
    }

    cout << endl;
}