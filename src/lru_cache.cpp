#include "../include/lru_cache.h"

#include <iostream>
#include <stdexcept>

using namespace std;

/**
 * @file lru_cache.cpp
 * @brief Implements the thread-safe LRU cache using a list and hash map.
 *
 * The list maintains MRU -> LRU order, while the hash map provides
 * O(1) average-time access to cache entries.
 */

// Creates an LRU cache with the given capacity.
LRUCache::LRUCache(int _capacity)
{
    if (_capacity < 0)
    {
        throw invalid_argument(
            "Cache capacity cannot be negative");
    }

    this->capacity = _capacity;
}

// Inserts a new key-value pair or updates an existing key.
void LRUCache::put(const string &key, const string &value)
{
    // Lock the cache while modifying shared data.
    lock_guard<mutex> lock(cacheMutex);

    // Capacity 0 means caching is disabled.
    if (capacity == 0)
    {
        return;
    }

    auto it = cacheMap.find(key);

    // Remove the old entry if the key already exists.
    if (it != cacheMap.end())
    {
        cacheList.erase(it->second);
        cacheMap.erase(it);
    }

    // Add the new entry at the front as MRU.
    cacheList.push_front({key, value});
    cacheMap[key] = cacheList.begin();

    // Remove the least recently used entry if capacity is exceeded.
    if (cacheMap.size() > capacity)
    {
        auto it = cacheList.rbegin();

        cacheMap.erase(it->first);
        cacheList.pop_back();
    }
}

// Retrieves a value and moves the key to the MRU position.
optional<string> LRUCache::get(const string &key)
{
    // Lock the cache while accessing and updating shared data.
    lock_guard<mutex> lock(cacheMutex);

    auto it = cacheMap.find(key);

    // Key is not present in the cache.
    if (it == cacheMap.end())
    {
        return nullopt;
    }

    // Save the value before moving the entry.
    string value = it->second->second;

    // Move the accessed entry to the front as MRU.
    cacheList.splice(
        cacheList.begin(),
        cacheList,
        it->second);

    return value;
}

// Removes a key from the cache if it exists.
void LRUCache::remove(const string &key)
{
    // Lock the cache while modifying shared data.
    lock_guard<mutex> lock(cacheMutex);

    auto it = cacheMap.find(key);

    if (it == cacheMap.end())
    {
        return;
    }

    cacheList.erase(it->second);
    cacheMap.erase(it);
}

// Prints cache contents from MRU to LRU.
void LRUCache::print() const
{
    // Lock because this function reads shared cache data.
    lock_guard<mutex> lock(cacheMutex);

    if (cacheList.empty())
    {
        cout << "CACHE EMPTY" << endl;
        return;
    }

    cout << "MRU → LRU" << endl;

    for (const auto &entry : cacheList)
    {
        cout << entry.first << " → " << entry.second << endl;
    }
}

// Returns the current number of entries in the cache.
int LRUCache::size() const
{
    // Lock because this function reads shared cache data.
    lock_guard<mutex> lock(cacheMutex);

    return static_cast<int>(cacheMap.size());
}
