#ifndef WAL_H
#define WAL_H

#include <fstream>
#include <mutex>
#include <string>
#include <vector>

#include "lru_cache.h"

using namespace std;

/*
 * Represents one operation stored in the WAL.
 */
struct WALRecord
{
    long long sequenceNumber;
    string operation;
    string key;
    string value;
};

/*
 * Write-Ahead Log.
 *
 * Stores every modification made to the key-value store
 * and provides recovery through WAL replay.
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

    // WAL owns a file stream and mutex, so copying is disabled.
    WAL(const WAL &) = delete;
    WAL &operator=(const WAL &) = delete;

    // Log a new SET operation.
    bool logSet(
        const string &key,
        const string &value);

    // Log a new DELETE operation.
    bool logDelete(
        const string &key);

    // Flush buffered WAL data to disk.
    void flush();

    // Replay WAL operations into the key-value store.
    bool replay(
        LRUCache &store);

    /*
     * Log operations using an explicitly supplied sequence number.
     *
     * Used by backup nodes so that they preserve the
     * primary's sequence numbers during replication.
     */

    bool logSetWithSequence(
        long long sequenceNumber,
        const string &key,
        const string &value);

    bool logDeleteWithSequence(
        long long sequenceNumber,
        const string &key);

    // Return the latest sequence number in the WAL.
    long long getLastSequenceNumber();

    // Return WAL records starting from the given sequence number.
    vector<WALRecord> getRecordsFromSequence(
        long long startSequence);
};

#endif