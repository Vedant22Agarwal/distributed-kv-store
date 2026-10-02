# Baseline Benchmark

## Environment

* Architecture: Distributed Sharded Key-Value Store
* Shards: 2
* Nodes: 6 (2 primaries, 4 backups)
* Router port: 8080
* Clients: 1
* Benchmark: 1,000 SET + 1,000 GET operations

## Results

| Metric                |          Result |
| --------------------- | --------------: |
| Total operations      |           2,000 |
| Successful operations |           2,000 |
| Failed operations     |               0 |
| Elapsed time          |   0.840 seconds |
| Throughput            | 2379.74 ops/sec |
| Average latency       |        0.419 ms |
| Median (p50)          |        0.345 ms |
| p95 latency           |        0.690 ms |
| p99 latency           |        1.075 ms |

## Status

PASSED

## Notes

This is a sequential, single-client baseline after the networking robustness changes.

Use these measurements to compare future concurrency and performance optimizations.
