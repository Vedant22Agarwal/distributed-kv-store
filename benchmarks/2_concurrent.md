# Phase 15.2 — Concurrent Benchmark Results

## Objective

Measure the Distributed Sharded Key-Value Store's end-to-end performance when multiple clients send requests concurrently through the router.

## Test Configuration

* Concurrent clients: 4
* Configured operations: 4,000
* Successful operations: 4,000
* Failed operations: 0
* System components: two shard primaries, four backups, and the router
* Test script: `2_benchmark_concurrent.py`

## Results

| Metric                |          Result |
| --------------------- | --------------: |
| Concurrent clients    |               4 |
| Operations configured |           4,000 |
| Successful operations |           4,000 |
| Failed operations     |               0 |
| Elapsed time          |   0.829 seconds |
| Throughput            | 4827.55 ops/sec |
| Average latency       |        0.810 ms |
| Median latency (p50)  |        0.747 ms |
| p95 latency           |        1.417 ms |
| p99 latency           |        1.977 ms |

## Baseline Comparison

The latest single-client baseline recorded 2,000 successful operations with no failures.

| Metric            |        Baseline |      Concurrent |
| ----------------- | --------------: | --------------: |
| Operations        |           2,000 |           4,000 |
| Throughput        | 2379.74 ops/sec | 4827.55 ops/sec |
| Average latency   |        0.419 ms |        0.810 ms |
| p50 latency       |        0.345 ms |        0.747 ms |
| p95 latency       |        0.690 ms |        1.417 ms |
| p99 latency       |        1.075 ms |        1.977 ms |
| Failed operations |               0 |               0 |

The concurrent run achieved approximately 2.03× the single-client throughput while handling twice as many operations. Latency percentiles increased under concurrent load.

These measurements describe the observed runs and do not, by themselves, establish the cause of the latency increase.

## Historical Observation

An earlier Phase 15.2 run, before the networking changes, recorded:

* Throughput: 3139.82 ops/sec
* Average latency: 1.260 ms
* p50 latency: 1.159 ms
* p95 latency: 1.800 ms
* p99 latency: 2.688 ms
* Failed operations: 0

This result is retained as a separate historical observation rather than replacing it.

## Outcome

**Phase 15.2: Concurrent benchmark completed successfully.**

All 4,000 configured operations succeeded, and no operation failures were reported.

## Notes and Limitations

* These are end-to-end measurements through the router, not isolated measurements of a single shard.
* The current architecture allows concurrent client requests through the router.
* Results are local development measurements, not production performance guarantees.
* Benchmark results can vary between runs due to system load and local networking conditions.
* All shard nodes and the router should remain running during subsequent benchmark scripts unless a test specifically requires a restart.

## Next Step

Proceed to **Phase 15.3 — shard-level workload comparison**.

Generate keys that map to each shard using the same 64-bit FNV-1a hash and compare the workloads separately. Because requests still pass through the router, describe the results as end-to-end per-shard workload measurements rather than isolated shard capacity.
