#!/usr/bin/env python3
"""Compare end-to-end router performance for workloads mapped to each shard.

Run from the project root while both shard primaries, all backups, and the
router are running:
    python3 3_benchmark_shards.py

This measures the complete client -> router -> shard path. It does not isolate
the raw capacity of an individual shard.
"""

import socket
import statistics
import time

HOST = "127.0.0.1"
PORT = 8080
KEYS_PER_SHARD = 1000
SOCKET_TIMEOUT_SECONDS = 5
MASK_64 = (1 << 64) - 1
FNV_OFFSET_BASIS = 14695981039346656037
FNV_PRIME = 1099511628211


def fnv1a_64(text):
    """Match the project's 64-bit FNV-1a hash."""
    value = FNV_OFFSET_BASIS
    for byte in text.encode("utf-8"):
        value ^= byte
        value = (value * FNV_PRIME) & MASK_64
    return value


def target_shard(key):
    # This benchmark assumes the project's shard selection is hash % 2.
    return fnv1a_64(key) % 2


def make_keys(shard_id, count):
    keys = []
    candidate = 0
    while len(keys) < count:
        key = f"phase15_shard{shard_id}_bench_{candidate}"
        if target_shard(key) == shard_id:
            keys.append(key)
        candidate += 1
    return keys


class RouterClient:
    def __init__(self, host, port):
        self.sock = socket.create_connection(
            (host, port), timeout=SOCKET_TIMEOUT_SECONDS
        )
        self.sock.settimeout(SOCKET_TIMEOUT_SECONDS)
        self.reader = self.sock.makefile("r", encoding="utf-8", newline="\n")

    def command(self, command):
        self.sock.sendall((command + "\n").encode("utf-8"))
        response = self.reader.readline()
        if response == "":
            raise ConnectionError("Router closed the connection")
        return response.rstrip("\r\n")

    def close(self):
        try:
            self.reader.close()
        finally:
            self.sock.close()


def is_error(response):
    upper = response.strip().upper()
    return (
        upper.startswith("ERROR")
        or upper.startswith("ERR ")
        or upper in {"FAIL", "FAILED"}
    )


def benchmark_shard(shard_id, keys):
    client = RouterClient(HOST, PORT)
    latencies_ms = []
    successful = 0
    failed = 0
    operation_count = len(keys) * 2
    start = time.perf_counter()

    try:
        for index, key in enumerate(keys):
            value = f"phase15_value_{shard_id}_{index}"

            # SET
            operation_start = time.perf_counter()
            try:
                response = client.command(f"SET {key} {value}")
                elapsed_ms = (time.perf_counter() - operation_start) * 1000
                latencies_ms.append(elapsed_ms)
                if is_error(response):
                    failed += 1
                else:
                    successful += 1
            except (OSError, ConnectionError, TimeoutError) as exc:
                failed += 1
                print(f"Shard {shard_id}: SET failed for {key}: {exc}")
                break

            # GET and verify that the stored value appears in the response.
            operation_start = time.perf_counter()
            try:
                response = client.command(f"GET {key}")
                elapsed_ms = (time.perf_counter() - operation_start) * 1000
                latencies_ms.append(elapsed_ms)

                if is_error(response) or value not in response:
                    failed += 1
                else:
                    successful += 1
            except (OSError, ConnectionError, TimeoutError) as exc:
                failed += 1
                print(f"Shard {shard_id}: GET failed for {key}: {exc}")
                break
    finally:
        elapsed_seconds = time.perf_counter() - start
        client.close()

    measured = len(latencies_ms)
    throughput = successful / elapsed_seconds if elapsed_seconds > 0 else 0.0

    print(f"\n===== SHARD {shard_id} WORKLOAD =====")
    print(f"Keys mapped to shard:  {len(keys)}")
    print(f"Operations configured: {operation_count}")
    print(f"Successful operations: {successful}")
    print(f"Failed operations:     {failed}")
    print(f"Elapsed time:          {elapsed_seconds:.3f} seconds")
    print(f"Throughput:            {throughput:.2f} successful ops/sec")

    if measured:
        ordered = sorted(latencies_ms)
        p50 = statistics.median(ordered)

        def percentile(values, percent):
            index = max(0, min(len(values) - 1, int((percent / 100) * len(values) + 0.999999) - 1))
            return values[index]

        print(f"Average latency:       {statistics.mean(ordered):.3f} ms")
        print(f"Median (p50):           {p50:.3f} ms")
        print(f"p95 latency:            {percentile(ordered, 95):.3f} ms")
        print(f"p99 latency:            {percentile(ordered, 99):.3f} ms")
    else:
        print("Latency statistics:    unavailable (no operations completed)")

    return successful, failed


def main():
    print("Phase 15.3 — Shard Workload Comparison")
    print(f"Router: {HOST}:{PORT}")
    print(f"Keys per shard: {KEYS_PER_SHARD}")
    print("Each key receives one SET and one GET.")
    print("Keep all nodes and the router running during this test.")

    try:
        keys_by_shard = {
            shard_id: make_keys(shard_id, KEYS_PER_SHARD)
            for shard_id in (0, 1)
        }
    except KeyboardInterrupt:
        print("\nBenchmark interrupted while generating keys.")
        return

    total_successful = 0
    total_failed = 0

    # Run each shard's workload separately for a clearer comparison.
    for shard_id in (0, 1):
        successful, failed = benchmark_shard(shard_id, keys_by_shard[shard_id])
        total_successful += successful
        total_failed += failed

    print("\n===== OVERALL RESULT =====")
    print(f"Successful operations: {total_successful}")
    print(f"Failed operations:     {total_failed}")
    if total_failed == 0 and total_successful == KEYS_PER_SHARD * 4:
        print("SHARD WORKLOAD COMPARISON PASSED")
    else:
        print("SHARD WORKLOAD COMPARISON COMPLETED WITH FAILURES")
        print("Check the router protocol and node logs before interpreting results.")


if __name__ == "__main__":
    main()



