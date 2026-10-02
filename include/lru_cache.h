#pragma once // used to write for include get added only once in main. file

#include <list>
#include <string>
#include <unordered_map>
#include <optional>

#include <mutex>
#include <stdexcept>

using namespace std;

/**
 * @class LRUCache
 * @brief Thread-safe Least Recently Used (LRU) cache.
 *
 * The cache stores key-value pairs with a configurable capacity.
 *
 * Capacity:
 * - Positive value: Cache enabled.
 * - Zero: Cache disabled.
 * - Negative value: Throws invalid_argument.
 *
 * Data structures:
 * - Doubly linked list:
 *   Maintains entries from MRU (front)
 *   to LRU (back).
 *
 * - Unordered map:
 *   Maps each key to its corresponding list iterator.
 *   Enables O(1) average lookup.
 *
 * Thread Safety:
 * - All shared cache data is protected by cacheMutex.
 * - Each public operation locks the mutex before
 *   accessing or modifying cache data.
 *
 * Average Time Complexity:
 * - put(): O(1)
 * - get(): O(1)
 * - remove(): O(1)
 * - size(): O(1)
 *
 * Space Complexity: O(capacity)
 *
 * Note:
 * - The cache is volatile and loses data when the process exits.
 * - It will later be integrated into each KV node.
 */
class LRUCache
{
private:
    // Stores key-value pairs in MRU -> LRU order.
    using CacheList = list<pair<string, string>>;

    // Iterator pointing to a node in the doubly linked list.
    using CacheIterator = CacheList::iterator;

    // Maximum number of entries allowed in the cache.
    int capacity;

    // Front = Most Recently Used (MRU).
    // Back = Least Recently Used (LRU).
    CacheList cacheList;

    // Maps a key to its corresponding list node.
    unordered_map<string, CacheIterator> cacheMap;

    // Protects all cache data structures.
    mutable mutex cacheMutex; 

public:
    // Initializes the cache with a fixed capacity.
    // Throws invalid_argument for negative capacity.
    explicit LRUCache(int capacity);

    // Prevent copying because mutex cannot be copied.
    LRUCache(const LRUCache &) = delete;
    LRUCache &operator=(const LRUCache &) = delete;

    // Prevent moving because the cache contains iterators
    // and a mutex that should remain tied to this object.
    LRUCache(LRUCache &&) = delete;
    LRUCache &operator=(LRUCache &&) = delete;

    // Inserts or updates a key-value pair.
    // The inserted/updated key becomes MRU.
    void put(const string &key, const string &value);

    // Retrieves a value if present.
    // Moves the accessed key to the MRU position.
    // Returns nullopt if the key does not exist.
    optional<string> get(const string &key);

    // Removes a key from the cache if it exists.
    void remove(const string &key);

    // Prints all cache entries from MRU to LRU.
    void print() const;

    // Returns the current number of entries.
    int size() const;
};
