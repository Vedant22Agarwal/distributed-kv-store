#ifndef REPLICATION_H
#define REPLICATION_H

#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>

#include "wal.h"

using namespace std;

enum class ReplicaState
{
    HEALTHY,
    LAGGING,
    DOWN,
    STARTING
};

/*
 * Stores information about a backup node.
 */
struct BackupNode
{
    string host;
    int port;

    // Last sequence number persisted by this backup.
    long long lastSequenceNumber;

    ReplicaState state;
};

/*
 * Handles replication between the primary and backup nodes.
 */
class ReplicationManager
{
private:
    // Registered backup nodes.
    vector<BackupNode> backups;

    // Protects the backup list and replication operations.
    mutex replicationMutex;

    // Primary WAL used to recover missing records.
    WAL *primaryWAL;

    thread healthThread;
    atomic<bool> healthMonitoring;


    atomic<int> healthCheckIntervalSeconds;

    void setReplicaState(
        BackupNode &backup,
        ReplicaState newState);

    // Send the complete message through TCP.
    bool sendAll(
        int socket,
        const string &message);

    // Receive a newline-terminated response.
    bool receiveLine(
        int socket,
        string &response);

    // Connect to a backup node.
    bool connectToBackup(
        const BackupNode &backup,
        int &socket);

public:
    // Create a replication manager using the primary WAL.
    explicit ReplicationManager(WAL *wal)
        : primaryWAL(wal),
          healthMonitoring(false),
          healthCheckIntervalSeconds(5)
    {
    }

    ~ReplicationManager();

    // Register a backup node.
    void addBackup(
        const string &host,
        int port);

    // Get the latest persisted sequence number from a backup.
    bool getBackupStatus(
        BackupNode &backup);

    // Replicate a SET operation to all backups.
    bool replicateSet(
        long long sequenceNumber,
        const string &key,
        const string &value);

    // Replicate a DELETE operation to all backups.
    bool replicateDelete(
        long long sequenceNumber,
        const string &key);

    // Catch up one backup using missing WAL records.
    bool catchUpBackup(
        BackupNode &backup);

    // Catch up all registered backups.
    void catchUpAllBackups();

    bool getFromHealthyReplica(
        const string &key,
        string &value);

    void startHealthMonitoring();
    void stopHealthMonitoring();

    void setHealthCheckInterval(int seconds);
};

#endif