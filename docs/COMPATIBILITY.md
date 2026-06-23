# Compatibility notes

`fum::unordered_map` aims to be a behavioural drop-in for `std::unordered_map`
under `[unord.map]` and `[unord.req]`. This document records the guarantees it
upholds and the single intentional deviation.

## Iterator, reference and pointer validity

Matches `std::unordered_map` exactly:

| operation                         | iterators                         | references / pointers |
|-----------------------------------|-----------------------------------|-----------------------|
| `insert` / `emplace` (no rehash)  | unaffected                        | unaffected            |
| `insert` / `emplace` (rehash)     | **invalidated**                   | **unaffected**        |
| `rehash` / `reserve`              | **invalidated**                   | **unaffected**        |
| `erase`                           | only the erased element's         | only the erased one's |
| `clear`                           | all                               | all                   |

The pointer/reference-stability column is the property that separates this
container from "flat" hash maps and makes it a genuine drop-in. It holds because
elements live in a segmented arena whose storage is never moved or reallocated;
only the internal index table is rebuilt on rehash.

> Note: in practice `fum::unordered_map` even keeps iterators valid across an
> `insert` that does *not* rehash, which is stricter than the standard requires.
> Do not rely on this — the standard permits invalidation, and `rehash` does
> invalidate iterators here as allowed.

## Complexity

| operation                              | average | worst case |
|----------------------------------------|---------|------------|
| `find`, `count`, `contains`, `at`, `[]`| O(1)    | O(size)    |
| `insert`, `emplace`, `erase`           | O(1)    | O(size)    |
| iterating the whole container          | O(size) | O(size)    |

Worst case requires a hostile or degenerate hash. The built-in bit-mixing of
`std::hash` outputs makes the common adversarial patterns (e.g. keys that are
multiples of the table size) behave as average-case; see
`tests/test_adversarial.cpp`.

Iteration is O(size) — never O(bucket_count) — because live elements are tracked
in a dense vector, so a sparsely populated table still iterates in time
proportional to the number of elements.

## The one intentional deviation: `max_load_factor`

`std::unordered_map`'s default `max_load_factor()` is `1.0`. Open addressing
cannot operate at a load factor of `1.0` (the table must always retain at least
one empty slot), so:

* the **default** `max_load_factor()` is `0.8`;
* a value you set with `max_load_factor(z)` is honoured for growth decisions, but
  the table still guarantees at least one empty bucket, so an effective factor of
  `1.0` behaves as "as full as safely possible" rather than literally 1.0;
* non-positive values are ignored (reset to the default), mirroring the leeway
  `[unord.req]` gives implementations ("`z` ... the container ... may ...").

Every other observable behaviour of the hash-policy interface (`load_factor`,
`rehash`, `reserve`, `bucket_count` growth) matches the standard's intent.

## Bucket interface semantics

Open addressing has no per-bucket linked lists, so "bucket" is defined as *the
set of live elements sharing a home slot*:

* `bucket(key)` returns that home slot (`mixed_hash(key)` mapped to the table);
* `bucket_size(n)` and the local iterators (`begin(n)`/`end(n)`) report/visit
  exactly those elements;
* the sum of `bucket_size(n)` over all buckets equals `size()`.

These functions are provided for full API compatibility but are a **slow path**
(a scan), since real code rarely uses them; the fast lookup path does not go
through buckets.

## `max_size`

Element slots are addressed with a 32-bit index, so `max_size()` is
approximately `2^32 - 2` (~4.29 billion) elements. This is far above any
realistic in-memory map and keeps the index table compact (and therefore cache
friendly).

## Thread safety

Identical to `std::unordered_map`: concurrent reads of the same container are
safe; any mutation requires external synchronisation. The container performs no
internal locking.

## Allocators

Fully allocator aware. The supplied allocator is rebound for the internal
arena, index table and bookkeeping vectors. `propagate_on_container_copy_assignment`,
`propagate_on_container_move_assignment`, `propagate_on_container_swap` and
`is_always_equal` are all respected, and stateful allocators with unequal
instances trigger element-wise move/copy as required.
