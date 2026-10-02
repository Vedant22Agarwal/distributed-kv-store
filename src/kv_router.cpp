#include "../include/kv_router.h"

KVRouter::KVRouter(ShardRouter& router)
    : shardRouter(router)
{
}

int KVRouter::routeSet(const string& key) const
{
    return shardRouter.route(key);
}

int KVRouter::routeGet(const string& key) const
{
    return shardRouter.route(key);
}

int KVRouter::routeDelete(const string& key) const
{
    return shardRouter.route(key);
}