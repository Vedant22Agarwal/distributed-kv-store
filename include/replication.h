#ifndef REPLICATION_H
#define REPLICATION_H

#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>

#include "wal.h"

using namespace std;

/**
 * @file replication.h
 * @brief Handles replication and recovery of data between a primary
 *        node and its backup nodes.
 */

// Represents the current state of a backup node.
enum class ReplicaState
{
    HEALTHY,
    LAGGING,
    DOWN,
    STARTING
};

/**
 * Stores connection and replication information about a backup node.
 */
struct BackupNode
{
    string host;
    int port;

    // Last WAL sequence number successfully persisted by this backup.
    long long lastSequenceNumber;

    // Current health/state of the backup.
    ReplicaState state;
};

/**
 * Manages replication from the primary node to its backup nodes.
 */
class ReplicationManager
{
private:
    // List of registered backup nodes.
    vector<BackupNode> backups;

    // Protects shared replication state and backup information.
    mutex replicationMutex;

    // Primary WAL used to send missing records during recovery.
    WAL *primaryWAL;

    // Background thread used to monitor backup health.
    thread healthThread;

    // Controls whether health monitoring is running.
    atomic<bool> healthMonitoring;

    // Interval between health checks, in seconds.
    atomic<int> healthCheckIntervalSeconds;

    // Updates the state of a backup node.
    void setReplicaState(
        BackupNode &backup,
        ReplicaState newState);

    // Sends the complete message over TCP.
    bool sendAll(
        int socket,
        const string &message);

    // Receives a newline-terminated response from a backup.
    bool receiveLine(
        int socket,
        string &response);

    // Establishes a TCP connection with a backup node.
    bool connectToBackup(
        const BackupNode &backup,
        int &socket);

public:
    // Creates a replication manager using the primary's WAL.
    explicit ReplicationManager(WAL *wal)
        : primaryWAL(wal),
          healthMonitoring(false),
          healthCheckIntervalSeconds(5)
    {
    }

    ~ReplicationManager();

    // Registers a new backup node.
    void addBackup(
        const string &host,
        int port);

    // Retrieves the latest persisted sequence number from a backup.
    bool getBackupStatus(
        BackupNode &backup);

    // Replicates a SET operation to the backup nodes.
    bool replicateSet(
        long long sequenceNumber,
        const string &key,
        const string &value);

    // Replicates a DELETE operation to the backup nodes.
    bool replicateDelete(
        long long sequenceNumber,
        const string &key);

    // Sends missing WAL records to a backup to bring it up to date.
    bool catchUpBackup(
        BackupNode &backup);

    // Attempts to catch up all registered backup nodes.
    void catchUpAllBackups();

    // Reads a value from a healthy backup replica.
    bool getFromHealthyReplica(
        const string &key,
        string &value);

    // Starts the background backup health-monitoring thread.
    void startHealthMonitoring();

    // Stops the background health-monitoring thread.
    void stopHealthMonitoring();

    // Sets the interval between health checks.
    void setHealthCheckInterval(int seconds);
};

#endif
