#ifndef SHARD_MANAGER_H
#define SHARD_MANAGER_H

#include <string>
#include <vector>
#include <cstdint>
#include <stdexcept>
#include <mutex>

using namespace std;

/**
 * @file shard_manager.h
 * @brief Stores shard topology and determines the shard for each key.
 */

// Stores the primary and backup information for a shard.
struct Shard
{
    int id;
    string primaryHost;
    int primaryPort;
    vector<pair<string, int>> backups;

    // Creates a shard with the given ID.
    explicit Shard(int shardId)
        : id(shardId), primaryPort(0)
    {
    }
};

// Manages shards, their primaries, and their backup nodes.
class ShardManager
{
public:
    // Creates a manager with the specified number of shards.
    explicit ShardManager(int shardCount);

    // Returns the shard ID responsible for the given key.
    int getShard(const string& key) const;

    // Returns the total number of configured shards.
    int getShardCount() const;

    // Sets the primary node for a shard.
    void setPrimary(int shardId, const string& host, int port);

    // Adds a backup node to a shard.
    void addBackup(int shardId, const string& host, int port);

    // Returns the configuration information for a shard.
    Shard getShardInfo(int shardId) const;

    // Promotes a backup node to become the shard's primary.
    bool promoteBackup(int shardId, const string& host, int port);

private:
    // Stores the configuration of all shards.
    vector<Shard> shards;

    // Protects shard configuration from concurrent access.
    mutable mutex managerMutex;

    // Computes the FNV-1a hash of a key.
    uint64_t fnv1a(const string& key) const;
};

#endif
