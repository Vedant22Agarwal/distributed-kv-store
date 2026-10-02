# Phase 01: Basic Key-Value Store

## Objective

Build an in-memory key-value store supporting SET, GET, and DELETE operations.

## Concepts Learned

- C++ unordered_map
- Hash-based storage
- Key-value operations
- Average O(1) lookup, insertion, and deletion

## Design Decisions

- Used unordered_map for in-memory storage.
- Keys and values are strings.
- SET updates the value if the key already exists.

## Testing

- Inserted and retrieved city.
- Inserted and retrieved age.
- Deleted city and verified NOT_FOUND.

## Complexity

Average:
- SET: O(1)
- GET: O(1)
- DELETE: O(1)

Space Complexity: O(n)

## Limitations

- Data is lost when the process terminates.
- No networking.
- No thread safety.
- No persistence.

## Next Phase

Implement an LRU cache to improve frequently accessed key performance.