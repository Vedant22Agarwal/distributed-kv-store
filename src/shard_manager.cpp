
/**
 * @file shard_manager.cpp
 * @brief Implements shard configuration and key-to-shard mapping.
 */

#include "../include/shard_manager.h"

#include <algorithm>

// Creates the requested number of shards.
ShardManager::ShardManager(int shardCount)
{
    if (shardCount <= 0)
        throw invalid_argument("Shard count must be greater than 0");

    for (int i = 0; i < shardCount; i++)
        shards.emplace_back(i);
}

// Computes the 64-bit FNV-1a hash for a key.
uint64_t ShardManager::fnv1a(const string& key) const
{
    const uint64_t FNV_OFFSET_BASIS = 14695981039346656037ULL;
    const uint64_t FNV_PRIME = 1099511628211ULL;

    uint64_t hash = FNV_OFFSET_BASIS;

    for (unsigned char c : key)
    {
        hash ^= c;
        hash *= FNV_PRIME;
    }

    return hash;
}

// Maps a key to its shard using the FNV-1a hash.
int ShardManager::getShard(const string& key) const
{
    return fnv1a(key) % shards.size();
}

// Returns the total number of configured shards.
int ShardManager::getShardCount() const
{
    return shards.size();
}

// Sets the primary node for a shard.
void ShardManager::setPrimary(
    int shardId,
    const string& host,
    int port)
{
    lock_guard<mutex> lock(managerMutex);

    if (shardId < 0 || shardId >= (int)shards.size())
        throw out_of_range("Invalid shard ID");

    if (host.empty() || port <= 0 || port > 65535)
        throw invalid_argument("Invalid primary address");

    shards[shardId].primaryHost = host;
    shards[shardId].primaryPort = port;
}

// Adds a backup node to a shard.
void ShardManager::addBackup(
    int shardId,
    const string& host,
    int port)
{
    lock_guard<mutex> lock(managerMutex);

    if (shardId < 0 || shardId >= (int)shards.size())
        throw out_of_range("Invalid shard ID");

    if (host.empty() || port <= 0 || port > 65535)
        throw invalid_argument("Invalid backup address");

    shards[shardId].backups.emplace_back(host, port);
}

// Returns a copy of the shard configuration.
Shard ShardManager::getShardInfo(int shardId) const
{
    lock_guard<mutex> lock(managerMutex);

    if (shardId < 0 || shardId >= (int)shards.size())
        throw out_of_range("Invalid shard ID");

    return shards[shardId];
}

// Promotes a configured backup to become the shard primary.
bool ShardManager::promoteBackup(
    int shardId,
    const string& host,
    int port)
{
    lock_guard<mutex> lock(managerMutex);

    if (shardId < 0 || shardId >= (int)shards.size())
        return false;

    Shard& shard = shards[shardId];

    auto it = find(
        shard.backups.begin(),
        shard.backups.end(),
        make_pair(host, port)
    );

    if (it == shard.backups.end())
        return false;

    shard.primaryHost = it->first;
    shard.primaryPort = it->second;
    shard.backups.erase(it);

    return true;
}