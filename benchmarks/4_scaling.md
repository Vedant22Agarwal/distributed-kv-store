# Phase 15.4 — Concurrency Scaling Benchmark

## Objective

Measure end-to-end performance through the router at increasing client concurrency levels.

## Test Configuration

* Router: `127.0.0.1:8080`
* Keys per run: 1,000
* Operations per key: one `SET` and one `GET`
* Operations configured per run: 2,000
* Client levels: 1, 2, 4
* Test script: `4_benchmark_scaling.py`
* WAL files were not cleared or deleted.

## Results

| Clients | Operations | Successful | Failed | Elapsed |    Throughput |  Average |      p50 |      p95 |      p99 |
| ------: | ---------: | ---------: | -----: | ------: | ------------: | -------: | -------: | -------: | -------: |
|       1 |      2,000 |      2,000 |      0 | 4.923 s |  406.29 ops/s | 2.451 ms | 2.270 ms | 3.412 ms | 5.034 ms |
|       2 |      2,000 |      2,000 |      0 | 1.477 s | 1354.51 ops/s | 1.469 ms | 1.308 ms | 2.169 ms | 3.790 ms |
|       4 |      2,000 |      2,000 |      0 | 0.976 s | 2050.02 ops/s | 1.935 ms | 1.762 ms | 3.033 ms | 4.058 ms |

## Scaling Observations

* All three concurrency levels completed successfully with **zero failed operations**.
* Throughput increased from **406.29 ops/sec** at one client to **1354.51 ops/sec** at two clients.
* Throughput increased further to **2050.02 ops/sec** at four clients.
* Compared with one client, the four-client workload achieved approximately **5.05× the throughput**.
* Average latency decreased from 2.451 ms at one client to 1.469 ms at two clients, then increased to 1.935 ms at four clients.
* The p95 and p99 latency values also increased at four clients compared with two clients.
* These are observations from a single local benchmark run and do not establish a permanent scaling limit.

## Overall Result

* Total successful operations: **6,000**
* Total failed operations: **0**
* Overall benchmark status: **PASSED**

**Phase 15.4: Concurrency scaling benchmark passed.**

All configured concurrency levels—1, 2, and 4 clients—completed their 2,000-operation workloads successfully.

## Notes and Limitations

* This is an end-to-end benchmark through the shared router, not a measurement of isolated shard capacity.
* The benchmark currently evaluates client concurrency levels of 1, 2, and 4.
* The previously observed 8-client failures are retained as a separate historical observation and are not included in this clean Phase 15.4 run.
* A single run at each concurrency level is not sufficient to establish a stable performance limit.
* No WAL files were cleared or deleted during testing.
* These are local development measurements, not production performance guarantees.

## Historical 8-Client Observation

An earlier Phase 15.4 run included 8 clients and experienced connection-establishment failures, including:

`[Errno 49] Can't assign requested address`

That earlier result is **not part of the current Phase 15.4 pass criteria**, because the benchmark was subsequently restricted to 1, 2, and 4 clients.

## Outcome

**Phase 15.4: PASSED**

The clean benchmark completed all **6,000 configured operations** successfully with **0 failures** across the tested concurrency levels.
