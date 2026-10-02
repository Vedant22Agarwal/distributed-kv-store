#include "../include/wal.h"

#include <algorithm>
#include <iostream>
#include <sstream>

using namespace std;

/*
 * Opens the WAL, restores the latest sequence number,
 * and then opens the file in append mode.
 */
WAL::WAL(
    const string &path)
    : filePath(path),
      nextSequenceNumber(1),
      lastSequenceNumber(0)
{
    ifstream inputFile(filePath);

    long long highestSequenceNumber = 0;
    string line;

    while (getline(inputFile, line))
    {
        if (line.empty())
            continue;

        stringstream input(line);

        long long sequenceNumber;

        if (input >> sequenceNumber)
        {
            highestSequenceNumber =
                max(
                    highestSequenceNumber,
                    sequenceNumber);
        }
    }

    inputFile.close();

    lastSequenceNumber =
        highestSequenceNumber;

    nextSequenceNumber =
        lastSequenceNumber + 1;

    logFile.open(
        filePath,
        ios::out | ios::app);

    if (!logFile.is_open())
    {
        cerr << "Failed to open WAL file: "
             << filePath
             << endl;
    }
}

/*
 * Closes the WAL file.
 */
WAL::~WAL()
{
    lock_guard<mutex> lock(walMutex);

    if (logFile.is_open())
        logFile.close();
}

/*
 * Generates the next sequence number.
 */
long long WAL::getNextSequenceNumber()
{
    return nextSequenceNumber++;
}

/*
 * Logs a SET operation.
 */
bool WAL::logSet(
    const string &key,
    const string &value)
{
    lock_guard<mutex> lock(walMutex);

    if (!logFile.is_open())
        return false;

    long long sequenceNumber =
        getNextSequenceNumber();

    /*
     * Write the WAL record.
     */
    logFile
        << sequenceNumber
        << " SET "
        << key
        << " "
        << value
        << "\n";

    /*
     * Check whether the write succeeded.
     */
    if (!logFile.good())
        return false;

    /*
     * Flush the record so the stream buffer
     * is written to the WAL file.
     */
    logFile.flush();

    /*
     * Verify that the flush succeeded.
     */
    if (!logFile.good())
        return false;

    /*
     * Only now update the in-memory sequence.
     *
     * Therefore:
     *
     * lastSequenceNumber
     *        =
     * persisted WAL sequence
     */
    lastSequenceNumber =
        sequenceNumber;

    return true;
}
/*
 * Logs a DELETE operation.
 */
bool WAL::logDelete(
    const string &key)
{
    lock_guard<mutex> lock(walMutex);

    if (!logFile.is_open())
        return false;

    long long sequenceNumber =
        getNextSequenceNumber();

    /*
     * Write the WAL record.
     */
    logFile
        << sequenceNumber
        << " DEL "
        << key
        << "\n";

    /*
     * Check whether the write succeeded.
     */
    if (!logFile.good())
        return false;

    /*
     * Flush the record.
     */
    logFile.flush();

    /*
     * Verify the flush.
     */
    if (!logFile.good())
        return false;

    /*
     * Only after successful persistence do we
     * update the in-memory sequence.
     */
    lastSequenceNumber =
        sequenceNumber;

    return true;
}
/*
 * Flushes buffered WAL data.
 */
void WAL::flush()
{
    lock_guard<mutex> lock(walMutex);

    if (logFile.is_open())
        logFile.flush();
}

/*
 * Replays the WAL into the provided cache.
 *
 * Sequence numbers must be continuous:
 *
 * 1, 2, 3, 4, ...
 */
bool WAL::replay(
    LRUCache &store)
{
    lock_guard<mutex> lock(walMutex);

    ifstream inputFile(filePath);

    if (!inputFile.is_open())
    {
        cerr << "Failed to open WAL for replay: "
             << filePath
             << endl;

        return false;
    }

    string line;

    long long expectedSequenceNumber = 1;

    while (getline(inputFile, line))
    {
        if (line.empty())
            continue;

        stringstream input(line);

        long long sequenceNumber;
        string operation;

        if (!(input >> sequenceNumber >> operation))
        {
            cerr << "Invalid WAL record: "
                 << line
                 << endl;

            return false;
        }

        /*
         * Verify WAL sequence continuity.
         */
        if (sequenceNumber != expectedSequenceNumber)
        {
            cerr << "WAL sequence error. Expected: "
                 << expectedSequenceNumber
                 << ", Received: "
                 << sequenceNumber
                 << endl;

            return false;
        }

        expectedSequenceNumber++;

        /*
         * SET
         */
        if (operation == "SET")
        {
            string key;
            input >> key;

            if (key.empty())
            {
                cerr << "Invalid SET record: "
                     << line
                     << endl;

                continue;
            }

            string value;
            getline(input, value);

            if (!value.empty() &&
                value[0] == ' ')
            {
                value.erase(0, 1);
            }

            if (value.empty())
            {
                cerr << "Invalid SET record: "
                     << line
                     << endl;

                continue;
            }

            store.put(
                key,
                value);
        }

        /*
         * DELETE
         */
        else if (operation == "DEL")
        {
            string key;
            input >> key;

            if (key.empty())
            {
                cerr << "Invalid DEL record: "
                     << line
                     << endl;

                continue;
            }

            store.remove(key);
        }

        /*
         * Unknown operation.
         */
        else
        {
            cerr << "Unknown WAL operation: "
                 << operation
                 << endl;
        }
    }

    if (inputFile.bad())
    {
        cerr << "Error while reading WAL file"
             << endl;

        return false;
    }

    inputFile.close();

    return true;
}

/*
 * Logs a SET operation using a specific sequence number.
 *
 * Used by backup nodes to preserve the primary's sequence.
 */
bool WAL::logSetWithSequence(
    long long sequenceNumber,
    const string &key,
    const string &value)
{
    lock_guard<mutex> lock(walMutex);

    if (!logFile.is_open())
        return false;

    logFile
        << sequenceNumber
        << " SET "
        << key
        << " "
        << value
        << "\n";

    if (!logFile.good())
        return false;

    /*
     * Persist the record before updating the
     * in-memory sequence.
     */
    logFile.flush();

    if (!logFile.good())
        return false;

    nextSequenceNumber =
        max(
            nextSequenceNumber,
            sequenceNumber + 1);

    lastSequenceNumber =
        max(
            lastSequenceNumber,
            sequenceNumber);

    return true;
}
/*
 * Logs a DELETE operation using a specific sequence number.
 */
bool WAL::logDeleteWithSequence(
    long long sequenceNumber,
    const string &key)
{
    lock_guard<mutex> lock(walMutex);

    if (!logFile.is_open())
        return false;

    logFile
        << sequenceNumber
        << " DEL "
        << key
        << "\n";

    if (!logFile.good())
        return false;

    /*
     * Persist the record before updating the
     * in-memory sequence.
     */
    logFile.flush();

    if (!logFile.good())
        return false;

    nextSequenceNumber =
        max(
            nextSequenceNumber,
            sequenceNumber + 1);

    lastSequenceNumber =
        max(
            lastSequenceNumber,
            sequenceNumber);

    return true;
}
/*
 * Returns the latest sequence number in the WAL.
 */
long long WAL::getLastSequenceNumber()
{
    lock_guard<mutex> lock(walMutex);

    return lastSequenceNumber;
}

/*
 * Reads WAL records starting from startSequence.
 */
vector<WALRecord> WAL::getRecordsFromSequence(
    long long startSequence)
{
    lock_guard<mutex> lock(walMutex);

    vector<WALRecord> records;

    ifstream inputFile(filePath);

    if (!inputFile.is_open())
    {
        cerr << "Failed to open WAL for reading: "
             << filePath
             << endl;

        return records;
    }

    string line;

    while (getline(inputFile, line))
    {
        if (line.empty())
            continue;

        stringstream input(line);

        WALRecord record;

        if (!(input >>
              record.sequenceNumber >>
              record.operation))
        {
            cerr << "Invalid WAL record: "
                 << line
                 << endl;

            continue;
        }

        if (record.sequenceNumber < startSequence)
            continue;

        /*
         * SET
         */
        if (record.operation == "SET")
        {
            input >> record.key;

            if (record.key.empty())
                continue;

            getline(
                input,
                record.value);

            if (!record.value.empty() &&
                record.value[0] == ' ')
            {
                record.value.erase(0, 1);
            }

            if (record.value.empty())
                continue;
        }

        /*
         * DELETE
         */
        else if (record.operation == "DEL")
        {
            input >> record.key;

            if (record.key.empty())
                continue;

            record.value = "";
        }

        /*
         * Unknown operation.
         */
        else
        {
            cerr << "Unknown WAL operation: "
                 << record.operation
                 << endl;

            continue;
        }

        records.push_back(record);
    }

    inputFile.close();

    return records;
}