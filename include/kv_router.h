#ifndef KV_ROUTER_H
#define KV_ROUTER_H

#include "shard_router.h"

#include <string>

using namespace std;

/**
 * @file kv_router.h
 * @brief Routes key-value operations to the appropriate shard.
 *
 * KVRouter provides a simple interface for routing SET, GET,
 * and DELETE operations based on the key.
 */

class KVRouter
{
public:
    // Initializes the router with a ShardRouter instance.
    explicit KVRouter(ShardRouter& router);

    // Returns the shard responsible for the given key for SET.
    int routeSet(const string& key) const;

    // Returns the shard responsible for the given key for GET.
    int routeGet(const string& key) const;

    // Returns the shard responsible for the given key for DELETE.
    int routeDelete(const string& key) const;

private:
    // Reference to the shard router used for routing.
    ShardRouter& shardRouter;
};

#endif
