#ifndef WAL_H
#define WAL_H

#include <fstream>
#include <mutex>
#include <string>
#include <vector>

#include "lru_cache.h"

using namespace std;

/**
 * @file wal.h
 * @brief Provides Write-Ahead Logging for persistence and recovery.
 */

// Represents a single operation stored in the WAL.
struct WALRecord
{
    long long sequenceNumber;
    string operation;
    string key;
    string value;
};

/**
 * Manages the Write-Ahead Log (WAL).
 *
 * Stores every modification made to the key-value store
 * and supports recovery by replaying logged operations.
 */
class WAL
{
private:
    // WAL file opened in append mode.
    ofstream logFile;

    // Protects concurrent WAL operations.
    mutex walMutex;

    // Path of the WAL file.
    string filePath;

    // Next sequence number to assign.
    long long nextSequenceNumber;

    // Latest sequence number stored in the WAL.
    long long lastSequenceNumber;

    // Generates the next sequence number.
    long long getNextSequenceNumber();

public:
    // Opens or creates the WAL file.
    explicit WAL(const string &path);

    // Closes the WAL file.
    ~WAL();

    // Copying is disabled because WAL owns a file stream and mutex.
    WAL(const WAL &) = delete;
    WAL &operator=(const WAL &) = delete;

    // Logs a new SET operation.
    bool logSet(
        const string &key,
        const string &value);

    // Logs a new DELETE operation.
    bool logDelete(
        const string &key);

    // Flushes buffered WAL data to disk.
    void flush();

    // Replays WAL operations to restore the key-value store.
    bool replay(
        LRUCache &store);

    // Logs a SET operation with a specific sequence number.
    // Used by backup nodes during replication.
    bool logSetWithSequence(
        long long sequenceNumber,
        const string &key,
        const string &value);

    // Logs a DELETE operation with a specific sequence number.
    // Used by backup nodes during replication.
    bool logDeleteWithSequence(
        long long sequenceNumber,
        const string &key);

    // Returns the latest sequence number stored in the WAL.
    long long getLastSequenceNumber();

    // Returns WAL records starting from the given sequence number.
    vector<WALRecord> getRecordsFromSequence(
        long long startSequence);
};

#endif
