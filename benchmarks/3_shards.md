# Shard Workload Comparison

## Objective

Compare the end-to-end performance of workloads whose keys map to Shard 0 and Shard 1 through the router.

## Test Configuration

* Router: `127.0.0.1:8080`
* Shards tested: 2
* Keys per shard: 1,000
* Operations per key: 1 `SET` and 1 `GET`
* Operations per shard: 2,000
* Total configured operations: 4,000
* Test script: `3_benchmark_shards.py`
* Hashing: 64-bit FNV-1a, modulo 2

## Results

| Metric                |         Shard 0 |         Shard 1 |
| --------------------- | --------------: | --------------: |
| Keys mapped to shard  |           1,000 |           1,000 |
| Operations configured |           2,000 |           2,000 |
| Successful operations |           2,000 |           2,000 |
| Failed operations     |               0 |               0 |
| Elapsed time          |   0.831 seconds |   0.697 seconds |
| Throughput            | 2405.87 ops/sec | 2870.46 ops/sec |
| Average latency       |        0.414 ms |        0.347 ms |
| Median latency (p50)  |        0.357 ms |        0.341 ms |
| p95 latency           |        0.661 ms |        0.486 ms |
| p99 latency           |        1.216 ms |        0.627 ms |

## Observations

* Both shard workloads completed all 2,000 operations without reported failures.
* Shard 1 recorded higher throughput in this run: **2870.46 ops/sec** compared with **2405.87 ops/sec** for Shard 0.
* Shard 1 also recorded lower average latency, p50, p95, and p99 latency in this run.
* The observed throughput difference was approximately **19.3%** in favor of Shard 1.
* The differences observed in a single run do not establish a persistent performance advantage for either shard.

## Outcome

**Phase 15.3: Shard workload comparison passed.**

* Total successful operations: 4,000
* Total failed operations: 0

## Notes and Limitations

* These are end-to-end measurements through the shared router, not isolated measurements of each shard's raw capacity.
* The results represent one local development run and are not production performance guarantees.
* No node restart or WAL cleanup was required for this benchmark.
* Differences between the two workloads may vary across repeated runs because of local system and networking conditions.

## Next Step

Proceed to **Phase 15.4 — performance optimization and comparison**.

Preserve these results as the pre-optimization reference, change one component at a time, and rerun the same workloads to compare throughput and latency.
