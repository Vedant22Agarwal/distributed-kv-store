#ifndef KV_ROUTER_H
#define KV_ROUTER_H

#include "shard_router.h"

#include <string>

using namespace std;

class KVRouter
{
public:
    explicit KVRouter(ShardRouter& router);

    int routeSet(const string& key) const;
    int routeGet(const string& key) const;
    int routeDelete(const string& key) const;

private:
    ShardRouter& shardRouter;
};

#endif