#include "../include/kv_router.h"

/**
 * @file kv_router.cpp
 * @brief Implements routing of SET, GET, and DELETE operations.
 */

// Initializes KVRouter with a reference to the ShardRouter.
KVRouter::KVRouter(ShardRouter &router) : shardRouter(router)
{
}

// Routes a SET operation to the shard responsible for the key.
int KVRouter::routeSet(const string &key) const
{
    return shardRouter.route(key);
}

// Routes a GET operation to the shard responsible for the key.
int KVRouter::routeGet(const string &key) const
{
    return shardRouter.route(key);
}

// Routes a DELETE operation to the shard responsible for the key.
int KVRouter::routeDelete(const string &key) const
{
    return shardRouter.route(key);
}
