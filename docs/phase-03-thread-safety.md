
# Phase 3: Thread Safety in LRU Cache

## 1. Objective

Make the LRU Cache thread-safe so that multiple threads can safely perform
read and write operations simultaneously without corrupting shared data.

The cache should maintain consistent internal state when accessed by
concurrent clients.

---

## 2. Why Thread Safety Is Required

Our LRU Cache uses two shared data structures:

1. `std::list`
   - Stores key-value pairs.
   - Maintains MRU → LRU ordering.

2. `std::unordered_map`
   - Maps keys to list iterators.
   - Enables O(1) average lookup.

Both structures are modified during cache operations.

If multiple threads access them simultaneously without synchronization,
the following problems may occur:

- Data races
- Invalid iterators
- Corrupted cache state
- Program crashes
- Incorrect cache results

Therefore, we need a synchronization mechanism.

---

## 3. Mutex

A mutex (mutual exclusion) allows only one thread at a time to access
a protected critical section.

We use:

```cpp
mutable mutex cacheMutex;
```

The mutex protects:

- `cacheList`
- `cacheMap`
- Cache size
- MRU/LRU ordering

### Critical Section

A critical section is the part of the code that accesses shared data.

For example:

```cpp
lock_guard<mutex> lock(cacheMutex);
```

This locks the mutex when the `lock_guard` is created.

The mutex is automatically unlocked when the `lock_guard` goes out of scope.

This is called RAII (Resource Acquisition Is Initialization).

---

## 4. Why Use lock_guard?

Instead of manually locking and unlocking:

```cpp
cacheMutex.lock();

// Shared operations

cacheMutex.unlock();
```

We use:

```cpp
lock_guard<mutex> lock(cacheMutex);
```

Advantages:

- Automatically unlocks the mutex.
- Prevents forgetting to unlock.
- Works correctly when exceptions occur.
- Makes the code safer and easier to maintain.

---

## 5. Thread-Safe Operations

### put()

The `put()` operation locks the mutex before:

1. Checking whether the key exists.
2. Removing the old entry.
3. Inserting the new entry.
4. Updating the hash map.
5. Evicting the LRU entry if required.

```cpp
void LRUCache::put(
    const string& key,
    const string& value
)
{
    lock_guard<mutex> lock(cacheMutex);

    // Cache modification operations
}
```

### get()

The `get()` operation also requires a mutex because it modifies the
LRU ordering.

When an entry is accessed, it moves to the front of the list.

Therefore, `get()` is not a read-only operation internally.

```cpp
optional<string> LRUCache::get(const string& key)
{
    lock_guard<mutex> lock(cacheMutex);

    // Find entry and update MRU order
}
```

### remove()

The `remove()` operation locks the mutex before deleting an entry.

```cpp
void LRUCache::remove(const string& key)
{
    lock_guard<mutex> lock(cacheMutex);

    // Remove the entry
}
```

### print()

The `print()` operation locks the mutex before reading the cache list.

This prevents printing while another thread modifies the list.

```cpp
void LRUCache::print() const
{
    lock_guard<mutex> lock(cacheMutex);

    // Print cache contents
}
```

### size()

The `size()` operation also locks the mutex before reading the map.

```cpp
int LRUCache::size() const
{
    lock_guard<mutex> lock(cacheMutex);

    return static_cast<int>(cacheMap.size());
}
```

---

## 6. Why Is the Mutex mutable?

The `print()` and `size()` methods are declared as `const`.

A const method cannot normally modify member variables.

However, locking a mutex changes its internal state.

Therefore, we declare the mutex as:

```cpp
mutable mutex cacheMutex;
```

This allows const methods to lock the mutex while keeping the logical cache
operation const.

---

## 7. Copy and Move Operations

The cache disables copy and move operations:

```cpp
LRUCache(const LRUCache&) = delete;
LRUCache& operator=(const LRUCache&) = delete;

LRUCache(LRUCache&&) = delete;
LRUCache& operator=(LRUCache&&) = delete;
```

Reasons:

- `std::mutex` cannot be copied.
- Cache iterators refer to internal list elements.
- Copying the cache requires careful handling of iterator relationships.
- Shared cache state should not be copied accidentally.

The same cache can still be shared between threads using references.

---

## 8. Multithreaded Test

### Test File

```cpp
#include "../include/lru_cache.h"

#include <iostream>
#include <thread>
#include <vector>

using namespace std;

void insertValues(LRUCache& cache, int threadId)
{
    for (int i = 0; i < 100; i++)
    {
        string key = "T" + to_string(threadId)
                   + "_K" + to_string(i);

        string value = "Value_" + to_string(i);

        cache.put(key, value);
    }
}

void readValues(LRUCache& cache, int threadId)
{
    for (int i = 0; i < 100; i++)
    {
        string key = "T" + to_string(threadId)
                   + "_K" + to_string(i);

        cache.get(key);
    }
}

int main()
{
    LRUCache cache(1000);

    vector<thread> threads;

    cout << "Starting concurrent PUT operations...\n";

    for (int i = 0; i < 4; i++)
    {
        threads.emplace_back(insertValues, ref(cache), i);
    }

    for (auto& t : threads)
    {
        t.join();
    }

    threads.clear();

    cout << "PUT operations completed.\n";

    cout << "Starting concurrent GET operations...\n";

    for (int i = 0; i < 4; i++)
    {
        threads.emplace_back(readValues, ref(cache), i);
    }

    for (auto& t : threads)
    {
        t.join();
    }

    cout << "GET operations completed.\n";

    cout << "Final cache size: "
         << cache.size() << endl;

    return 0;
}
```

---

## 9. Test Compilation

Compile the test using:

```bash
g++ -std=c++17 -pthread \
    src/lru_cache.cpp \
    tests/thread_safety_test.cpp \
    -o thread_test
```

Run:

```bash
./thread_test
```

---

## 10. Test Output

```text
Starting concurrent PUT operations...
PUT operations completed.
Starting concurrent GET operations...
GET operations completed.
Final cache size: 400
```

### Result Analysis

There are four threads performing PUT operations.

Each thread inserts 100 unique keys.

Total number of inserted keys:

```text
4 × 100 = 400
```

The cache capacity is 1000, so no eviction is expected.

The final cache size is:

```text
400
```

The concurrent GET operations complete successfully.

---

## 11. Thread Sanitizer

ThreadSanitizer can help detect data races.

Compile using:

```bash
g++ -std=c++17 -pthread -fsanitize=thread -g \
    src/lru_cache.cpp \
    tests/thread_safety_test.cpp \
    -o thread_test_tsan
```

Run:

```bash
./thread_test_tsan
```

A successful normal test does not prove the absence of every possible
data race. ThreadSanitizer provides additional dynamic analysis.

Note: ThreadSanitizer support can vary depending on the compiler and
macOS architecture.

---

## 12. Complexity Analysis

The mutex adds synchronization overhead, but the average algorithmic
complexities remain unchanged.

| Operation | Average Time Complexity |
|---|---|
| put() | O(1) |
| get() | O(1) |
| remove() | O(1) |
| size() | O(1) |

Space complexity:

```text
O(capacity)
```

Only one thread can execute a protected cache operation at a time.
Consequently, concurrent operations may wait for the mutex.

---

## 13. Limitations

The current implementation uses one global mutex for the entire cache.

Advantages:

- Simple implementation.
- Easy to reason about.
- Protects all shared structures consistently.

Limitations:

- Only one thread can execute a cache operation at a time.
- High contention may reduce performance.
- No reader-writer locking.
- No sharded locking.

Possible future improvements:

- Benchmark mutex contention.
- Consider finer-grained locking.
- Explore sharded caches.
- Measure throughput under concurrent workloads.

These optimizations should only be introduced after correctness testing.

---

## 14. Conclusion

In Phase 3, the LRU Cache was made thread-safe using `std::mutex` and
`std::lock_guard`.

All public cache operations now protect shared data.

The multithreaded test successfully completed concurrent PUT and GET
operations and produced a final cache size of 400.

The cache is now ready for the next stage: TCP networking.

---

## Next Phase

Phase 4: TCP Networking

Goals:

1. Understand TCP sockets.
2. Create a server.
3. Create a client.
4. Send requests between client and server.
5. Implement basic request-response communication.