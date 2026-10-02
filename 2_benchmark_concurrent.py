
import socket
import time
import statistics
from concurrent.futures import ThreadPoolExecutor, as_completed

HOST = "127.0.0.1"
PORT = 8080

CLIENTS = 4
OPS_PER_CLIENT = 500
TIMEOUT = 15


def percentile(values, p):
    values = sorted(values)
    index = int((p / 100) * len(values))
    return values[min(index, len(values) - 1)]


def run_client(client_id):
    latencies = []
    errors = 0
    completed = 0

    try:
        with socket.create_connection(
            (HOST, PORT), timeout=TIMEOUT
        ) as sock:
            sock.settimeout(TIMEOUT)
            reader = sock.makefile("r", encoding="utf-8")

            for i in range(OPS_PER_CLIENT):
                key = f"concurrent15_{client_id}_{i}"
                value = f"value_{client_id}_{i}"

                for command, expected in [
                    (f"SET {key} {value}", "ADDED"),
                    (f"GET {key}", f"VALUE: {value}"),
                ]:
                    start = time.perf_counter()

                    sock.sendall((command + "\n").encode())
                    response = reader.readline()

                    elapsed = time.perf_counter() - start

                    if not response:
                        errors += 1
                        return completed, errors, latencies

                    response = response.strip()

                    if response.startswith("ERROR") or response != expected:
                        errors += 1
                    else:
                        completed += 1
                        latencies.append(elapsed * 1000)

    except (OSError, TimeoutError) as e:
        print(f"Client {client_id} failed: {e}")
        errors += 1

    return completed, errors, latencies


def main():
    results = []
    start = time.perf_counter()

    with ThreadPoolExecutor(max_workers=CLIENTS) as executor:
        futures = [
            executor.submit(run_client, client_id)
            for client_id in range(CLIENTS)
        ]

        for future in as_completed(futures):
            results.append(future.result())

    elapsed = time.perf_counter() - start

    completed = sum(r[0] for r in results)
    errors = sum(r[1] for r in results)
    latencies = [
        latency
        for result in results
        for latency in result[2]
    ]

    print("\n===== CONCURRENT BENCHMARK =====")
    print(f"Concurrent clients:    {CLIENTS}")
    print(f"Operations configured: {CLIENTS * OPS_PER_CLIENT * 2}")
    print(f"Successful operations: {completed}")
    print(f"Failed operations:     {errors}")
    print(f"Elapsed time:          {elapsed:.3f} seconds")

    if elapsed > 0:
        print(f"Throughput:            {completed / elapsed:.2f} ops/sec")

    if latencies:
        print(f"Average latency:       {statistics.mean(latencies):.3f} ms")
        print(f"Median (p50):          {percentile(latencies, 50):.3f} ms")
        print(f"p95 latency:           {percentile(latencies, 95):.3f} ms")
        print(f"p99 latency:           {percentile(latencies, 99):.3f} ms")


if __name__ == "__main__":
    main()