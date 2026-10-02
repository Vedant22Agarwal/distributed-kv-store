#ifndef SHARD_ROUTER_H
#define SHARD_ROUTER_H

#include "shard_manager.h"

#include <string>

using namespace std;

class ShardRouter
{
public:
    explicit ShardRouter(ShardManager& manager);

    int route(const string& key) const;

    Shard routeToShard(const string& key) const;

    bool routeSet(
        const string& key,
        const string& value
    ) const;

    bool routeGet(
        const string& key
    ) const;

    bool routeDelete(
        const string& key
    ) const;

private:
    ShardManager& shardManager;
};

#endif