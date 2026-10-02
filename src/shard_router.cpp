#include "../include/shard_router.h"

ShardRouter::ShardRouter(ShardManager& manager)
    : shardManager(manager)
{
}

int ShardRouter::route(const string& key) const
{
    return shardManager.getShard(key);
}

Shard ShardRouter::routeToShard(const string& key) const
{
    int shardId = route(key);
    return shardManager.getShardInfo(shardId);
}