// benchmark.cpp
//
// Head-to-head micro-benchmark of fum::unordered_map against the standard
// library's std::unordered_map.  Because fum::unordered_map is a drop-in,
// API-compatible replacement, every operation below is exercised through the
// identical source code on both containers via a template parameter; only the
// container type differs.
//
// What it measures
// ----------------
// For a range of element counts and several <Key, T> configurations the
// program times the following operations and prints, per operation, the best
// (minimum) wall-clock time observed across repeated trials for each container
// plus the speedup ratio std/fum:
//
//   1. insert  (seq)   - insertion of sequential integer keys
//   2. insert  (rand)  - insertion of random integer keys
//   3. insert  (adv)   - insertion of ADVERSARIAL keys (i << 10).  These are
//                        multiples of a large power of two and collide badly in
//                        naive identity-hashed open-addressing tables; fum runs
//                        every hash through a splitmix64 finalizer, so this row
//                        demonstrates the library's hash-mixing defence.
//   4. find    (hit)   - successful lookups of present keys
//   5. find    (miss)  - failed lookups of absent keys
//   6. erase   (all)   - erase of every element one key at a time
//   7. iterate (sum)   - full range-based iteration summing the mapped values
//   8. mixed           - interleaved insert / find / erase workload
//
// Timing uses std::chrono::steady_clock, with a warmup pass and several repeats
// per measurement; the reported figure is the minimum, which is the most stable
// estimator for CPU-bound code.  A do_not_optimize() sink (inline asm, with a
// portable volatile fallback) prevents the optimizer from deleting the work.
// All randomness comes from a std::mt19937_64 seeded with a fixed constant so
// runs are reproducible.
//
// Recommended compile command (run from the repository root):
//
//   g++ -std=c++20 -O2 -DNDEBUG -Iinclude benchmarks/benchmark.cpp -o benchmark
//
// Usage:
//
//   ./benchmark            run with the default scale (a few seconds at -O2)
//   ./benchmark 1          quick smoke run (tiny element counts)
//   ./benchmark 4          larger / more thorough run
//
// The integer argument is a scale level selecting the element-count set; see
// element_counts_for_scale().  Higher levels use more and larger sizes.
//
// SPDX-License-Identifier: MIT

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

#include "fum/unordered_map.hpp"

namespace {

// ---------------------------------------------------------------------------
// Optimizer barrier
// ---------------------------------------------------------------------------
// Forces the compiler to assume `value` is read (and, for the pointer overload,
// that referenced memory is clobbered), so benchmarked work cannot be elided.

#if defined(__GNUC__) || defined(__clang__)
template <typename T>
inline void do_not_optimize(const T& value) {
    asm volatile("" : : "g"(value) : "memory");
}
inline void clobber_memory() { asm volatile("" : : : "memory"); }
#else
// Portable fallback: route the value through a volatile sink.
template <typename T>
inline void do_not_optimize(const T& value) {
    volatile const T* sink = &value;
    (void)sink;
}
inline void clobber_memory() {}
#endif

// ---------------------------------------------------------------------------
// Timing
// ---------------------------------------------------------------------------
using Clock = std::chrono::steady_clock;

[[nodiscard]] double to_milliseconds(Clock::duration duration) {
    return std::chrono::duration<double, std::milli>(duration).count();
}

// Run `action` `warmups` times untimed (to prime caches / branch predictors /
// the allocator), then `repeats` timed times, returning the best (minimum)
// elapsed time in milliseconds.  `action` is a nullary callable performing one
// full iteration of the operation under test.
template <typename Action>
[[nodiscard]] double measure_best_ms(int warmups, int repeats, Action action) {
    for (int i = 0; i < warmups; ++i) action();
    double best_ms = std::numeric_limits<double>::infinity();
    for (int trial = 0; trial < repeats; ++trial) {
        const Clock::time_point start = Clock::now();
        action();
        const Clock::time_point stop = Clock::now();
        const double elapsed_ms = to_milliseconds(stop - start);
        if (elapsed_ms < best_ms) best_ms = elapsed_ms;
    }
    return best_ms;
}

// ---------------------------------------------------------------------------
// Deterministic randomness
// ---------------------------------------------------------------------------
constexpr std::uint64_t kRandomSeed = 0x9E3779B97F4A7C15ULL;

[[nodiscard]] std::mt19937_64 make_rng() { return std::mt19937_64(kRandomSeed); }

// ---------------------------------------------------------------------------
// Key and value generation
// ---------------------------------------------------------------------------
// Keys for the three patterns are produced from a 64-bit "raw" index so that a
// single code path covers integer and string keys alike.  make_key converts a
// raw key into the container's actual key type.

enum class KeyPattern { Sequential, Random, Adversarial };

// Adversarial keys are multiples of a large power of two: their low bits are
// always zero, which maps every key to a tiny set of home buckets unless the
// hash is mixed.
constexpr int kAdversarialShift = 10;

template <typename Key>
[[nodiscard]] Key make_key(std::uint64_t raw);

template <>
[[nodiscard]] int make_key<int>(std::uint64_t raw) {
    return static_cast<int>(raw);
}
template <>
[[nodiscard]] std::uint64_t make_key<std::uint64_t>(std::uint64_t raw) {
    return raw;
}
template <>
[[nodiscard]] std::string make_key<std::string>(std::uint64_t raw) {
    // A textual key whose content (not just length) varies with `raw`, so that
    // std::hash<std::string> sees genuinely distinct inputs.
    return "key_" + std::to_string(raw);
}

// Build a vector of `count` raw key indices following the requested pattern.
// Random keys are drawn without the duplicates that would otherwise make the
// effective element count nondeterministic.
[[nodiscard]] std::vector<std::uint64_t> make_raw_keys(KeyPattern pattern,
                                                       std::size_t count) {
    std::vector<std::uint64_t> raw_keys;
    raw_keys.reserve(count);
    switch (pattern) {
        case KeyPattern::Sequential:
            for (std::size_t i = 0; i < count; ++i) {
                raw_keys.push_back(static_cast<std::uint64_t>(i));
            }
            break;
        case KeyPattern::Adversarial:
            for (std::size_t i = 0; i < count; ++i) {
                raw_keys.push_back(static_cast<std::uint64_t>(i)
                                   << kAdversarialShift);
            }
            break;
        case KeyPattern::Random: {
            std::mt19937_64 rng = make_rng();
            // Distinct values: hand out a shuffled prefix of [0, count) scaled
            // into a wide range so the bit patterns look random to the hasher.
            std::vector<std::uint64_t> pool(count);
            for (std::size_t i = 0; i < count; ++i) {
                pool[i] = static_cast<std::uint64_t>(i) * 2654435761ULL;
            }
            std::shuffle(pool.begin(), pool.end(), rng);
            raw_keys = std::move(pool);
            break;
        }
    }
    return raw_keys;
}

// Materialise the actual key objects once, up front, so generation cost (e.g.
// string construction) is excluded from the measured region.
template <typename Key>
[[nodiscard]] std::vector<Key> make_keys(KeyPattern pattern,
                                         std::size_t count) {
    const std::vector<std::uint64_t> raw_keys = make_raw_keys(pattern, count);
    std::vector<Key> keys;
    keys.reserve(raw_keys.size());
    for (const std::uint64_t raw : raw_keys) {
        keys.push_back(make_key<Key>(raw));
    }
    return keys;
}

// Absent keys, used for the failed-lookup benchmark: offset far beyond any
// present sequential/adversarial key so none of them can collide with a member.
template <typename Key>
[[nodiscard]] std::vector<Key> make_absent_keys(std::size_t count) {
    const std::uint64_t offset = std::uint64_t(1) << 40;
    std::vector<Key> keys;
    keys.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        keys.push_back(make_key<Key>(offset + i));
    }
    return keys;
}

// Mapped-value factory: produces a deterministic value for a given index for
// whatever the container's mapped_type happens to be.
template <typename T>
[[nodiscard]] T make_value(std::uint64_t index);

template <>
[[nodiscard]] int make_value<int>(std::uint64_t index) {
    return static_cast<int>(index);
}
template <>
[[nodiscard]] std::string make_value<std::string>(std::uint64_t index) {
    return "val_" + std::to_string(index);
}

// Fold a mapped value into a running 64-bit accumulator so iteration results
// can be funneled through do_not_optimize regardless of mapped_type.
[[nodiscard]] std::uint64_t value_hash(int value) {
    return static_cast<std::uint64_t>(value);
}
[[nodiscard]] std::uint64_t value_hash(const std::string& value) {
    return std::hash<std::string>{}(value);
}

// ---------------------------------------------------------------------------
// Results table
// ---------------------------------------------------------------------------
struct ResultRow {
    std::string operation;
    std::size_t element_count = 0;
    double std_ms = 0.0;
    double fum_ms = 0.0;
};

class ResultsTable {
  public:
    explicit ResultsTable(std::string title) : title_(std::move(title)) {}

    void add(ResultRow row) { rows_.push_back(std::move(row)); }

    void print() const {
        std::printf("\n=== %s ===\n", title_.c_str());
        std::printf("%-14s %10s %12s %12s %10s\n", "operation", "N", "std (ms)",
                    "fum (ms)", "speedup");
        std::printf("%-14s %10s %12s %12s %10s\n", "--------------", "---------",
                    "-----------", "-----------", "---------");
        for (const ResultRow& row : rows_) {
            const double speedup =
                row.fum_ms > 0.0 ? row.std_ms / row.fum_ms : 0.0;
            std::printf("%-14s %10zu %12.3f %12.3f %9.2fx\n",
                        row.operation.c_str(), row.element_count, row.std_ms,
                        row.fum_ms, speedup);
        }
    }

  private:
    std::string title_;
    std::vector<ResultRow> rows_;
};

// ---------------------------------------------------------------------------
// Per-operation benchmarks
// ---------------------------------------------------------------------------
// Each function is templated on the concrete Map type so the very same source
// runs against std::unordered_map and fum::unordered_map.  They return the best
// time in milliseconds for one full pass over `keys`.

// How many warmup and timed passes to run for a given element count.  Small
// inputs are cheap and noisy, so they get a warmup and many timed passes; large
// inputs are already long and stable (and each pass is expensive), so they skip
// the warmup and run only a couple of timed passes.  This bounds total runtime.
struct Trial {
    int warmups;
    int repeats;
};

[[nodiscard]] Trial trial_for(std::size_t element_count) {
    if (element_count <= 10000) return {1, 15};
    if (element_count <= 100000) return {1, 5};
    return {0, 2};  // >= ~1e6: skip warmup, minimal repeats
}

// Insert every key (with a generated value); returns time for the build.
template <typename Map, typename Key>
[[nodiscard]] double bench_insert(const std::vector<Key>& keys, Trial trial) {
    using Mapped = typename Map::mapped_type;
    return measure_best_ms(trial.warmups, trial.repeats, [&] {
        Map map;
        std::uint64_t index = 0;
        for (const Key& key : keys) {
            map.emplace(key, make_value<Mapped>(index++));
        }
        do_not_optimize(map.size());
        clobber_memory();
    });
}

// Look up keys that are all present; accumulate something derived from each
// hit so the lookups cannot be optimized away.
template <typename Map, typename Key>
[[nodiscard]] double bench_find_hit(const Map& map,
                                    const std::vector<Key>& keys, Trial trial) {
    return measure_best_ms(trial.warmups, trial.repeats, [&] {
        std::uint64_t found = 0;
        for (const Key& key : keys) {
            const auto it = map.find(key);
            if (it != map.end()) {
                found += value_hash(it->second);
            }
        }
        do_not_optimize(found);
    });
}

// Look up keys that are all absent.
template <typename Map, typename Key>
[[nodiscard]] double bench_find_miss(const Map& map,
                                     const std::vector<Key>& absent_keys,
                                     Trial trial) {
    return measure_best_ms(trial.warmups, trial.repeats, [&] {
        std::uint64_t misses = 0;
        for (const Key& key : absent_keys) {
            if (map.find(key) == map.end()) ++misses;
        }
        do_not_optimize(misses);
    });
}

// Iterate the whole container summing mapped values.
template <typename Map>
[[nodiscard]] double bench_iterate(const Map& map, Trial trial) {
    return measure_best_ms(trial.warmups, trial.repeats, [&] {
        std::uint64_t sum = 0;
        for (const auto& entry : map) {
            sum += value_hash(entry.second);
        }
        do_not_optimize(sum);
    });
}

// Erase every key one at a time.  A fresh, fully-populated copy is built inside
// each timed pass (outside is impossible since erasing is destructive); the
// build is unavoidable overhead common to both containers, so the comparison
// stays fair.
template <typename Map, typename Key>
[[nodiscard]] double bench_erase(const std::vector<Key>& keys, Trial trial) {
    using Mapped = typename Map::mapped_type;
    // Build a prototype once and copy it each pass to amortise construction.
    Map prototype;
    std::uint64_t index = 0;
    for (const Key& key : keys) {
        prototype.emplace(key, make_value<Mapped>(index++));
    }
    return measure_best_ms(trial.warmups, trial.repeats, [&] {
        Map map = prototype;
        std::uint64_t erased = 0;
        for (const Key& key : keys) {
            erased += map.erase(key);
        }
        do_not_optimize(erased);
        do_not_optimize(map.size());
        clobber_memory();
    });
}

// Mixed workload: walk the key set inserting; every few steps also look one up
// and erase an earlier one, so the table is continuously churning.
template <typename Map, typename Key>
[[nodiscard]] double bench_mixed(const std::vector<Key>& keys, Trial trial) {
    using Mapped = typename Map::mapped_type;
    const std::size_t count = keys.size();
    return measure_best_ms(trial.warmups, trial.repeats, [&] {
        Map map;
        std::uint64_t sink = 0;
        for (std::size_t i = 0; i < count; ++i) {
            map.emplace(keys[i], make_value<Mapped>(i));
            if ((i & 3u) == 0u) {
                const auto it = map.find(keys[i / 2]);
                if (it != map.end()) sink += value_hash(it->second);
            }
            if ((i & 7u) == 0u && i >= 16) {
                sink += map.erase(keys[i - 16]);
            }
        }
        do_not_optimize(sink);
        do_not_optimize(map.size());
        clobber_memory();
    });
}

// ---------------------------------------------------------------------------
// Driver for one <Key, T> configuration
// ---------------------------------------------------------------------------
// Runs the full operation suite at a single element count for both containers
// and appends the eight comparison rows to `table`.
template <typename Key, typename T>
void run_configuration_at(ResultsTable& table, std::size_t element_count) {
    using StdMap = std::unordered_map<Key, T>;
    using FumMap = fum::unordered_map<Key, T>;

    const std::vector<Key> sequential_keys =
        make_keys<Key>(KeyPattern::Sequential, element_count);
    const std::vector<Key> random_keys =
        make_keys<Key>(KeyPattern::Random, element_count);
    const std::vector<Key> adversarial_keys =
        make_keys<Key>(KeyPattern::Adversarial, element_count);
    const std::vector<Key> absent_keys =
        make_absent_keys<Key>(element_count);

    // For lookup/iteration we need populated containers.  Build them from the
    // RANDOM key set, not the sequential one: a hash map keyed by dense
    // sequential integers is an artificial best case for an identity hash (it
    // yields perfect cache locality and is exactly the pattern a plain array
    // would serve better).  Random keys are the representative workload, and
    // also the pattern hash maps actually exist to handle.
    const auto build = [&](auto& map) {
        std::uint64_t index = 0;
        for (const Key& key : random_keys) {
            map.emplace(key, make_value<T>(index++));
        }
    };
    StdMap std_map;
    FumMap fum_map;
    build(std_map);
    build(fum_map);

    const std::size_t n = element_count;
    const Trial t = trial_for(element_count);
    table.add({"insert (seq)", n, bench_insert<StdMap>(sequential_keys, t),
               bench_insert<FumMap>(sequential_keys, t)});
    table.add({"insert (rand)", n, bench_insert<StdMap>(random_keys, t),
               bench_insert<FumMap>(random_keys, t)});
    table.add({"insert (adv)", n, bench_insert<StdMap>(adversarial_keys, t),
               bench_insert<FumMap>(adversarial_keys, t)});
    table.add({"find (hit)", n, bench_find_hit(std_map, random_keys, t),
               bench_find_hit(fum_map, random_keys, t)});
    table.add({"find (miss)", n, bench_find_miss(std_map, absent_keys, t),
               bench_find_miss(fum_map, absent_keys, t)});
    table.add({"erase (all)", n, bench_erase<StdMap>(random_keys, t),
               bench_erase<FumMap>(random_keys, t)});
    table.add({"iterate (sum)", n, bench_iterate(std_map, t),
               bench_iterate(fum_map, t)});
    table.add({"mixed", n, bench_mixed<StdMap>(random_keys, t),
               bench_mixed<FumMap>(random_keys, t)});
}

// `max_element_count` caps the sizes actually run for this configuration.
// String-key maps allocate and hash a whole string per element and so cost
// roughly an order of magnitude more than integer keys; capping them keeps the
// default run within its time budget while integer keys still exercise the
// largest sizes.  Pass SIZE_MAX for no cap.
template <typename Key, typename T>
void run_configuration(const char* title,
                       const std::vector<std::size_t>& element_counts,
                       std::size_t max_element_count) {
    // Progress goes to stderr so stdout carries only the results tables.
    std::fprintf(stderr, "running %s ...\n", title);
    ResultsTable table(title);
    for (const std::size_t element_count : element_counts) {
        if (element_count > max_element_count) continue;
        const Clock::time_point start = Clock::now();
        run_configuration_at<Key, T>(table, element_count);
        std::fprintf(stderr, "  N=%zu done in %.2f s\n", element_count,
                     to_milliseconds(Clock::now() - start) / 1000.0);
    }
    table.print();
}

// ---------------------------------------------------------------------------
// Scale selection
// ---------------------------------------------------------------------------
// The optional command-line scale level chooses the element-count set.  The
// default (level 2) keeps the total run comfortably under ~15s at -O2; level 0
// is a tiny smoke test for verifying the program runs at all.
[[nodiscard]] std::vector<std::size_t> element_counts_for_scale(int scale) {
    switch (scale) {
        case 0:
            return {1000};
        case 1:
            return {10000, 100000};
        case 2:
            return {10000, 100000, 1000000};
        case 3:
            return {100000, 1000000, 4000000};
        default:
            return {100000, 1000000, 4000000, 8000000};
    }
}

// Largest element count run for the (more expensive) string-key configurations.
// Integer keys are never capped.  Higher scale levels raise the cap so a
// deliberately thorough run can stress string keys harder, at the cost of time.
[[nodiscard]] std::size_t string_cap_for_scale(int scale) {
    switch (scale) {
        case 0:
            return std::numeric_limits<std::size_t>::max();
        case 1:
            return std::numeric_limits<std::size_t>::max();
        case 2:
            return 100000;
        case 3:
            return 1000000;
        default:
            return std::numeric_limits<std::size_t>::max();
    }
}

[[nodiscard]] int parse_scale(int argc, char** argv) {
    if (argc < 2) return 2;  // default
    char* end = nullptr;
    const long parsed = std::strtol(argv[1], &end, 10);
    if (end == argv[1] || parsed < 0) {
        std::fprintf(stderr,
                     "warning: could not parse scale '%s'; using default\n",
                     argv[1]);
        return 2;
    }
    return static_cast<int>(parsed);
}

}  // namespace

int main(int argc, char** argv) {
    const int scale = parse_scale(argc, argv);
    const std::vector<std::size_t> element_counts =
        element_counts_for_scale(scale);

    std::printf("fum::unordered_map vs std::unordered_map benchmark\n");
    std::printf("scale level: %d   element counts:", scale);
    for (const std::size_t count : element_counts) {
        std::printf(" %zu", count);
    }
    std::printf("\n(speedup = std/fum; higher means fum is faster)\n");

    const std::size_t no_cap = std::numeric_limits<std::size_t>::max();
    const std::size_t string_cap = string_cap_for_scale(scale);
    if (string_cap < no_cap) {
        std::printf("(string-key configs capped at N=%zu for the time budget)\n",
                    string_cap);
    }

    run_configuration<int, int>("<int, int>", element_counts, no_cap);
    run_configuration<std::uint64_t, std::string>("<uint64_t, string>",
                                                  element_counts, string_cap);
    run_configuration<std::string, int>("<string, int>", element_counts,
                                        string_cap);

    std::printf("\ndone.\n");
    return 0;
}
