#include <iostream>
#include <string>
#include <vector>
#include <iomanip>

#include "../include/shard_manager.h"
#include "../include/shard_router.h"
#include "../include/kv_router.h"

using namespace std;

int main()
{
    const int SHARD_COUNT = 2;
    const int TOTAL_KEYS = 10000;

    ShardManager shardManager(SHARD_COUNT);
    ShardRouter shardRouter(shardManager);
    KVRouter kvRouter(shardRouter);

    vector<int> distribution(SHARD_COUNT, 0);

    int errors = 0;

    for (int i = 1; i <= TOTAL_KEYS; i++)
    {
        string key = "key" + to_string(i);

        int setShard = kvRouter.routeSet(key);
        int getShard = kvRouter.routeGet(key);
        int deleteShard = kvRouter.routeDelete(key);
        int repeatedShard = kvRouter.routeGet(key);

        if (setShard < 0 || setShard >= SHARD_COUNT ||
            getShard < 0 || getShard >= SHARD_COUNT ||
            deleteShard < 0 || deleteShard >= SHARD_COUNT)
        {
            errors++;
            continue;
        }

        if (setShard != getShard ||
            setShard != deleteShard ||
            setShard != repeatedShard)
        {
            errors++;
            continue;
        }

        distribution[setShard]++;
    }

    cout << "\n===== DISTRIBUTION TEST =====\n";
    cout << "Total keys tested: " << TOTAL_KEYS << "\n\n";

    for (int i = 0; i < SHARD_COUNT; i++)
    {
        double percentage =
            100.0 * distribution[i] / TOTAL_KEYS;

        cout << "Shard " << i << ": "
             << distribution[i] << " keys ("
             << fixed << setprecision(2)
             << percentage << "%)\n";
    }

    cout << "\nRouting errors: " << errors << "\n";

    if (errors == 0 &&
        distribution[0] > 0 &&
        distribution[1] > 0)
    {
        cout << "\nDISTRIBUTION TEST PASSED\n";
    }
    else
    {
        cout << "\nDISTRIBUTION TEST FAILED\n";
        return 1;
    }

    return 0;
}