
import socket
import time
import statistics

HOST = "127.0.0.1"
PORT = 8080
TOTAL_KEYS = 1000
TIMEOUT = 10

latencies = []
errors = 0
completed = 0


def percentile(values, p):
    values = sorted(values)
    index = int((p / 100) * len(values))
    return values[min(index, len(values) - 1)]


def main():
    global errors, completed

    try:
        sock = socket.create_connection((HOST, PORT), timeout=TIMEOUT)
        sock.settimeout(TIMEOUT)
        reader = sock.makefile("r", encoding="utf-8")
    except OSError as e:
        print("Connection failed:", e)
        return

    operations = []

    for i in range(TOTAL_KEYS):
        key = f"bench15_{i}"
        operations.append((f"SET {key} value_{i}", "ADDED"))
        operations.append((f"GET {key}", f"VALUE: value_{i}"))

    start = time.perf_counter()

    try:
        for command, expected in operations:
            op_start = time.perf_counter()

            sock.sendall((command + "\n").encode())
            response = reader.readline()

            elapsed = time.perf_counter() - op_start

            if not response:
                errors += 1
                print("Connection closed unexpectedly.")
                break

            response = response.strip()

            if response.startswith("ERROR") or response != expected:
                errors += 1
                if errors <= 10:
                    print(f"Failed: {command} -> {response}")
            else:
                completed += 1
                latencies.append(elapsed * 1000)

    except (OSError, TimeoutError) as e:
        errors += 1
        print("Benchmark interrupted:", e)

    total_time = time.perf_counter() - start
    sock.close()

    print("\n===== BASELINE BENCHMARK =====")
    print(f"Keys configured:       {TOTAL_KEYS}")
    print(f"Operations attempted:  {len(operations)}")
    print(f"Successful operations: {completed}")
    print(f"Failed operations:     {errors}")
    print(f"Elapsed time:          {total_time:.3f} seconds")

    if total_time > 0:
        print(f"Throughput:            {completed / total_time:.2f} ops/sec")

    if latencies:
        print(f"Average latency:       {statistics.mean(latencies):.3f} ms")
        print(f"Median (p50):          {percentile(latencies, 50):.3f} ms")
        print(f"p95 latency:           {percentile(latencies, 95):.3f} ms")
        print(f"p99 latency:           {percentile(latencies, 99):.3f} ms")


if __name__ == "__main__":
    main()