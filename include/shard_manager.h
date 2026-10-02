#ifndef SHARD_MANAGER_H
#define SHARD_MANAGER_H

#include <string>
#include <vector>
#include <cstdint>
#include <stdexcept>
#include <mutex>

using namespace std;

struct Shard
{
    int id;
    string primaryHost;
    int primaryPort;
    vector<pair<string, int>> backups;

    explicit Shard(int shardId)
        : id(shardId), primaryPort(0)
    {
    }
};

class ShardManager
{
public:
    explicit ShardManager(int shardCount);

    int getShard(const string& key) const;
    int getShardCount() const;

    void setPrimary(int shardId, const string& host, int port);
    void addBackup(int shardId, const string& host, int port);

    Shard getShardInfo(int shardId) const;

    bool promoteBackup(int shardId, const string& host, int port);

private:
    vector<Shard> shards;
    mutable mutex managerMutex;

    uint64_t fnv1a(const string& key) const;
};

#endif