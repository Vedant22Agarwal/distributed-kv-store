/**
 * @file shard_router.cpp
 * @brief Implements routing of keys to their corresponding shards.
 */

#include "../include/shard_router.h"

// Initializes the router with a shard manager.
ShardRouter::ShardRouter(ShardManager& manager)
    : shardManager(manager)
{
}

// Returns the shard ID responsible for the given key.
int ShardRouter::route(const string& key) const
{
    return shardManager.getShard(key);
}

// Returns the complete configuration of the shard for a key.
Shard ShardRouter::routeToShard(const string& key) const
{
    int shardId = route(key);

    return shardManager.getShardInfo(shardId);
}