#ifndef SHARD_ROUTER_H
#define SHARD_ROUTER_H

#include "shard_manager.h"

#include <string>

using namespace std;

/**
 * @file shard_router.h
 * @brief Routes requests to the appropriate shard based on the key.
 */

class ShardRouter
{
public:
    // Creates a router using the given ShardManager.
    explicit ShardRouter(ShardManager& manager);

    // Returns the shard ID responsible for the given key.
    int route(const string& key) const;

    // Returns the complete configuration of the shard for the key.
    Shard routeToShard(const string& key) const;

    // Routes a SET operation to the appropriate shard.
    bool routeSet(
        const string& key,
        const string& value
    ) const;

    // Routes a GET operation to the appropriate shard.
    bool routeGet(
        const string& key
    ) const;

    // Routes a DELETE operation to the appropriate shard.
    bool routeDelete(
        const string& key
    ) const;

private:
    // Reference to the shard manager used for routing.
    ShardManager& shardManager;
};

#endif
