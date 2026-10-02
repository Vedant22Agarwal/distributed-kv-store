#!/usr/bin/env python3
"""Phase 15.4: benchmark performance at different client concurrency levels.

Run from the DKV project root while both shard primaries, all backups, and
the router are running:
    python3 4_benchmark_scaling.py

This measures end-to-end performance through the router. It does not isolate
individual shard capacity. It creates fresh benchmark keys and does not delete
them or clear WAL files.
"""

import socket
import statistics
import time
from concurrent.futures import ThreadPoolExecutor, as_completed

HOST = "127.0.0.1"
PORT = 8080
TOTAL_KEYS = 1000
CLIENT_LEVELS = (1, 2, 4 )
SOCKET_TIMEOUT_SECONDS = 10


def is_error(response):
    upper = response.strip().upper()
    return (
        upper.startswith("ERROR")
        or upper.startswith("ERR ")
        or upper in {"FAIL", "FAILED"}
    )


def percentile(values, percent):
    ordered = sorted(values)
    if not ordered:
        return 0.0
    index = max(0, min(len(ordered) - 1, int((percent / 100) * len(ordered) + 0.999999) - 1))
    return ordered[index]


def run_client(client_id, keys):
    latencies_ms = []
    successful = 0
    failed = 0

    try:
        sock = socket.create_connection((HOST, PORT), timeout=SOCKET_TIMEOUT_SECONDS)
        sock.settimeout(SOCKET_TIMEOUT_SECONDS)
    except OSError as exc:
        return {
            "successful": 0,
            "failed": len(keys) * 2,
            "latencies": [],
            "error": f"Client {client_id} could not connect: {exc}",
        }

    try:
        reader = sock.makefile("r", encoding="utf-8", newline="\n")
        try:
            for key_index, key in enumerate(keys):
                value = f"phase15_scaling_client{client_id}_value{key_index}"

                for command, expected in (
                    (f"SET {key} {value}", None),
                    (f"GET {key}", value),
                ):
                    started = time.perf_counter()
                    try:
                        sock.sendall((command + "\n").encode("utf-8"))
                        response = reader.readline()
                        elapsed_ms = (time.perf_counter() - started) * 1000
                        if response == "":
                            failed += 1
                            return {
                                "successful": successful,
                                "failed": failed + (len(keys) - key_index - 1) * 2,
                                "latencies": latencies_ms,
                                "error": f"Client {client_id}: router closed the connection",
                            }

                        response = response.rstrip("\r\n")
                        latencies_ms.append(elapsed_ms)
                        if is_error(response) or (expected is not None and expected not in response):
                            failed += 1
                            print(
                                f"Client {client_id}, command {command!r}, "
                                f"response: {response!r}"
                            )
                        else:
                            successful += 1
                    except (OSError, TimeoutError) as exc:
                        failed += 1
                        return {
                            "successful": successful,
                            "failed": failed + (len(keys) - key_index - 1) * 2,
                            "latencies": latencies_ms,
                            "error": f"Client {client_id}: {exc}",
                        }
        finally:
            reader.close()
    finally:
        sock.close()

    return {
        "successful": successful,
        "failed": failed,
        "latencies": latencies_ms,
        "error": None,
    }


def split_keys(client_count, run_id):
    # Divide exactly TOTAL_KEYS across clients, including any remainder.
    counts = [TOTAL_KEYS // client_count] * client_count
    for i in range(TOTAL_KEYS % client_count):
        counts[i] += 1

    result = []
    next_key = 0
    for client_id, count in enumerate(counts):
        keys = [
            f"phase15_scale_run{run_id}_client{client_id}_key{index}"
            for index in range(next_key, next_key + count)
        ]
        next_key += count
        result.append(keys)
    return result


def run_level(client_count, run_id):
    key_groups = split_keys(client_count, run_id)
    all_latencies = []
    successful = 0
    failed = 0
    errors = []

    start = time.perf_counter()
    with ThreadPoolExecutor(max_workers=client_count) as pool:
        futures = [
            pool.submit(run_client, client_id, keys)
            for client_id, keys in enumerate(key_groups)
        ]
        for future in as_completed(futures):
            result = future.result()
            successful += result["successful"]
            failed += result["failed"]
            all_latencies.extend(result["latencies"])
            if result["error"]:
                errors.append(result["error"])

    elapsed = time.perf_counter() - start
    throughput = successful / elapsed if elapsed > 0 else 0.0

    print(f"\n===== {client_count} CLIENT(S) =====")
    print(f"Keys configured:       {TOTAL_KEYS}")
    print(f"Operations configured: {TOTAL_KEYS * 2}")
    print(f"Successful operations: {successful}")
    print(f"Failed operations:     {failed}")
    print(f"Elapsed time:          {elapsed:.3f} seconds")
    print(f"Throughput:            {throughput:.2f} successful ops/sec")

    if all_latencies:
        print(f"Average latency:       {statistics.mean(all_latencies):.3f} ms")
        print(f"Median (p50):           {statistics.median(all_latencies):.3f} ms")
        print(f"p95 latency:            {percentile(all_latencies, 95):.3f} ms")
        print(f"p99 latency:            {percentile(all_latencies, 99):.3f} ms")

    for error in errors:
        print(f"Error: {error}")

    return {
        "clients": client_count,
        "throughput": throughput,
        "average_latency": statistics.mean(all_latencies) if all_latencies else 0.0,
        "p95": percentile(all_latencies, 95),
        "p99": percentile(all_latencies, 99),
        "successful": successful,
        "failed": failed,
    }


def main():
    print("Phase 15.4 — Concurrency Scaling Benchmark")
    print(f"Router: {HOST}:{PORT}")
    print(f"Total keys per run: {TOTAL_KEYS}")
    print("Each key receives one SET and one GET.")
    print(f"Client levels: {', '.join(map(str, CLIENT_LEVELS))}")
    print("Keep all nodes and the router running; do not clear WAL files.")

    results = []
    for run_id, client_count in enumerate(CLIENT_LEVELS, start=1):
        results.append(run_level(client_count, run_id))

    print("\n===== SCALING SUMMARY =====")
    print(f"{'Clients':>8} {'Throughput ops/s':>18} {'Avg ms':>10} {'p95 ms':>10} {'p99 ms':>10} {'Failures':>10}")
    for result in results:
        print(
            f"{result['clients']:>8} "
            f"{result['throughput']:>18.2f} "
            f"{result['average_latency']:>10.3f} "
            f"{result['p95']:>10.3f} "
            f"{result['p99']:>10.3f} "
            f"{result['failed']:>10}"
        )

    total_failures = sum(result["failed"] for result in results)
    total_successful = sum(result["successful"] for result in results)
    print(f"\nTotal successful operations across all runs: {total_successful}")
    print(f"Total failed operations across all runs:     {total_failures}")
    if total_failures == 0:
        print("CONCURRENCY SCALING BENCHMARK PASSED")
    else:
        print("CONCURRENCY SCALING BENCHMARK COMPLETED WITH FAILURES")


if __name__ == "__main__":
    main()
