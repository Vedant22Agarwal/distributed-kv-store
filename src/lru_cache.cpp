
#include "../include/lru_cache.h"

#include <iostream>
#include <stdexcept>

using namespace std;

LRUCache::LRUCache(int _capacity)
{
    if (_capacity < 0)
    {
        throw invalid_argument(
            "Cache capacity cannot be negative"
        );
    }

    this->capacity = _capacity;
}

// Inserts a new key-value pair or updates an existing key.
void LRUCache::put(const string& key, const string& value)
{
    // Lock the cache for the duration of this function.
    lock_guard<mutex> lock(cacheMutex);

    // Capacity 0 means caching is disabled.
    if (capacity == 0)
    {
        return;
    }

    auto it = cacheMap.find(key);

    // If the key already exists, remove its old position.
    if (it != cacheMap.end())
    {
        cacheList.erase(it->second);
        cacheMap.erase(it);
    }

    // Insert the new entry at the front (MRU).
    cacheList.push_front({key, value});
    cacheMap[key] = cacheList.begin();

    // If capacity is exceeded, remove the LRU entry.
    if (cacheMap.size() > capacity)
    {
        auto it = cacheList.rbegin();

        cacheMap.erase(it->first);
        cacheList.pop_back();
    }
}

// Retrieves a value and moves the key to MRU.
optional<string> LRUCache::get(const string& key)
{
    // Lock the cache for the duration of this function.
    lock_guard<mutex> lock(cacheMutex);

    auto it = cacheMap.find(key);

    if (it == cacheMap.end())
    {
        return nullopt;
    }

    // Save the value before moving the node.
    string value = it->second->second;

    // Move the accessed node to the front (MRU).
    cacheList.splice(
        cacheList.begin(), // which position to add before
        cacheList, // List name
        it->second // element
    );

    return value;
}

// Removes a key from the cache.
void LRUCache::remove(const string& key)
{
    // Lock the cache for the duration of this function.
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
    // Lock because this function reads shared data.
    lock_guard<mutex> lock(cacheMutex);

    if (cacheList.empty())
    {
        cout << "CACHE EMPTY" << endl;
        return;
    }

    cout << "MRU → LRU" << endl;

    for (const auto& entry : cacheList)
    {
        cout << entry.first << " → " << entry.second << endl;
    }
}

// Returns the number of entries in the cache.
int LRUCache::size() const
{
    // Lock because this function reads shared data.
    lock_guard<mutex> lock(cacheMutex);

    return static_cast<int>(cacheMap.size());
}