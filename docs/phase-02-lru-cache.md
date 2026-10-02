
# Phase 2: LRU Cache

## 1. Objective

Implement a fixed-capacity Least Recently Used (LRU) cache that provides fast access to frequently used key-value pairs.

The cache will later be integrated into each node of our distributed key-value store.

---

## 2. Concepts Learned

- LRU (Least Recently Used) Cache
- `std::list`
- `std::unordered_map`
- Iterators
- `std::optional`
- Cache eviction
- MRU and LRU ordering
- Cache-aside pattern
- Exception handling
- Configurable cache capacity

---

## 3. Cache Capacity

The cache supports configurable capacity:

| Capacity | Behavior |
|---|---|
| Positive value | Cache enabled |
| `0` | Cache disabled |
| Negative value | Throws `invalid_argument` |

Example:

```cpp
LRUCache cache(1000);  // Cache enabled
LRUCache cache(0);     // Cache disabled
LRUCache cache(-1);    // Invalid configuration
```

A capacity of zero allows a node to operate without caching while still using the underlying storage engine.

---

## 4. Design

The cache uses two data structures.

### Doubly Linked List

A `std::list` maintains the order of entries.

```text
Front → Most Recently Used (MRU)
Back  → Least Recently Used (LRU)
```

Example:

```text
C → Cherry  (MRU)
B → Banana
A → Apple   (LRU)
```

### Unordered Map

The unordered map stores:

```text
Key → Iterator pointing to the list node
```

This allows us to find an entry in average O(1) time.

---

## 5. File Structure

```text
include/
└── lru_cache.h

src/
└── lru_cache.cpp

tests/
└── 2_lru_cache_test.cpp
```

---

## 6. Supported Operations

### `put(key, value)`

- Inserts a new key-value pair.
- Updates the value if the key already exists.
- Moves the key to the MRU position.
- Removes the LRU entry if capacity is exceeded.
- Does nothing if capacity is zero.

### `get(key)`

- Returns the value if the key exists.
- Moves the accessed key to the MRU position.
- Returns `nullopt` if the key does not exist.

### `remove(key)`

- Removes the specified key if it exists.
- Does nothing if the key is not present.

### `print()`

Displays cache entries from MRU to LRU.

### `size()`

Returns the current number of entries.

---

## 7. Time and Space Complexity

| Operation | Average Time Complexity |
|---|---|
| `put()` | O(1) |
| `get()` | O(1) |
| `remove()` | O(1) |
| `size()` | O(1) |

Space Complexity: **O(capacity)**

---

## 8. Implementation Details

### Why `std::list`?

A list allows us to move an existing node using `splice()` in O(1) time.

```cpp
cacheList.splice(
    cacheList.begin(),
    cacheList,
    it->second
);
```

This moves the accessed node to the front without copying the entire list.

### Why `std::unordered_map`?

The unordered map provides average O(1) lookup by key and stores iterators pointing to the corresponding list nodes.

### Eviction

When the cache exceeds its capacity:

1. Identify the last list element using `rbegin()`.
2. Remove its key from the unordered map.
3. Remove the last node using `pop_back()`.

---

## 9. Complete Test Code

File:

```text
tests/lru_cache_test.cpp
```

```cpp
#include "../include/lru_cache.h"

#include <iostream>
#include <stdexcept>

using namespace std;

int main()
{
    try
    {
        cout << "===== TEST 1: Normal Cache =====\n";

        LRUCache cache(3);

        cache.put("A", "Apple");
        cache.put("B", "Banana");
        cache.put("C", "Cherry");

        cache.print();

        cout << "\nGET A: ";

        auto value = cache.get("A");

        if (value.has_value())
        {
            cout << value.value() << endl;
        }
        else
        {
            cout << "NOT_FOUND" << endl;
        }

        cache.print();

        cout << "\nAdding D..." << endl;

        cache.put("D", "Durian");

        cache.print();

        cout << "\nGET B: ";

        auto result = cache.get("B");

        if (result.has_value())
        {
            cout << result.value() << endl;
        }
        else
        {
            cout << "NOT_FOUND" << endl;
        }

        cout << "\n===== TEST 2: Update Existing Key =====\n";

        cache.put("A", "Apricot");

        cache.print();

        auto updatedValue = cache.get("A");

        if (updatedValue.has_value())
        {
            cout << "Updated A: "
                 << updatedValue.value() << endl;
        }

        cout << "\n===== TEST 3: Remove Key =====\n";

        cache.remove("A");

        cache.print();

        cout << "Cache size: "
             << cache.size() << endl;

        cout << "\n===== TEST 4: Disabled Cache =====\n";

        LRUCache disabledCache(0);

        disabledCache.put("X", "Xylophone");

        auto disabledValue = disabledCache.get("X");

        if (!disabledValue.has_value())
        {
            cout << "Cache disabled: NOT_FOUND" << endl;
        }

        cout << "Disabled cache size: "
             << disabledCache.size() << endl;

        cout << "\n===== TEST 5: Invalid Capacity =====\n";

        try
        {
            LRUCache invalidCache(-1);

            cout << "ERROR: Exception was not thrown"
                 << endl;
        }
        catch (const invalid_argument& error)
        {
            cout << "Expected error: "
                 << error.what() << endl;
        }
    }
    catch (const exception& error)
    {
        cerr << "Unexpected error: "
             << error.what() << endl;

        return 1;
    }

    return 0;
}
```

---

## 10. Testing

The following scenarios were tested:

- Inserting multiple key-value pairs.
- Retrieving an existing key.
- Moving an accessed key to MRU.
- Evicting the LRU entry when capacity is exceeded.
- Retrieving an evicted key.
- Updating an existing key.
- Removing a key.
- Checking cache size.
- Disabling the cache using capacity `0`.
- Rejecting negative capacity using `invalid_argument`.

### Example Test

Initial cache:

```text
C → Cherry
B → Banana
A → Apple
```

After accessing A:

```text
A → Apple  (MRU)
C → Cherry
B → Banana (LRU)
```

After inserting D with capacity 3:

```text
D → Durian (MRU)
A → Apple
C → Cherry (LRU)
```

B is evicted because it was the least recently used entry.

### Disabled Cache

When capacity is `0`:

- `put()` does not store entries.
- `get()` returns `nullopt`.
- `size()` returns `0`.
- No exception is thrown.

### Invalid Capacity

When capacity is negative, the constructor throws:

```text
Error: Cache capacity cannot be negative
```

---

## 11. Cache-Aside Pattern

The LRU cache is a temporary storage layer, not permanent storage.

When an entry is evicted from the cache, its data will remain in the underlying database or storage engine.

In a future phase, we will implement the cache-aside pattern:

1. Check the cache.
2. If the key exists, return the cached value.
3. If there is a cache miss, read from storage.
4. Insert the retrieved value into the cache.
5. Return the value to the client.

---

## 12. Current Limitations

- The cache is not thread-safe yet.
- Permanent storage has not been integrated.
- Cache persistence is not implemented.
- There is no network communication.
- The cache operates locally within the process.
- Cache disabling is supported, but storage fallback will be implemented in a future phase.

---

## 13. Conclusion

Phase 2 successfully implemented an LRU cache with average O(1) insertion, retrieval, and removal operations.

The cache supports configurable capacity, including disabling caching with capacity `0`. Negative capacities are rejected using `invalid_argument`.

The cache is ready for the next phase: **Thread Safety**.

---

## 14. Next Phase

### Phase 3: Thread Safety

Goals:

- Protect cache operations using `std::mutex`.
- Prevent race conditions.
- Support concurrent access from multiple threads.
- Test the cache with multiple threads.