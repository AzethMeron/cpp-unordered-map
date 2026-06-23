// Shared, container-generic benchmark helpers used by sweep.cpp and
// compare_maps.cpp.  Every timing helper is templated on the map type, so the
// identical code path measures std::unordered_map, fum::unordered_map,
// boost::unordered_flat_map and absl::flat_hash_map alike.

#ifndef FUM_BENCH_COMMON_HPP
#define FUM_BENCH_COMMON_HPP

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <random>
#include <vector>

namespace bench {

using Clock = std::chrono::steady_clock;

// Defeat the optimizer so measured work is not elided.
template <typename T>
inline void do_not_optimize(T const& value) {
    asm volatile("" : : "g"(value) : "memory");
}

inline double now_ns() {
    return static_cast<double>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            Clock::now().time_since_epoch())
            .count());
}

// `count` distinct pseudo-random 64-bit keys (deterministic given `seed`).
inline std::vector<std::uint64_t> make_distinct_keys(std::size_t count,
                                                     std::uint64_t seed) {
    std::vector<std::uint64_t> keys;
    keys.reserve(count);
    std::mt19937_64 rng(seed);
    while (keys.size() < count) keys.push_back(rng());
    std::sort(keys.begin(), keys.end());
    keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
    while (keys.size() < count) keys.push_back(rng());
    std::shuffle(keys.begin(), keys.end(), rng);
    keys.resize(count);
    return keys;
}

// CSV sink: one row per measurement.
struct Csv {
    std::FILE* out;
    void row(const char* sweep, const char* container, const char* operation,
             double x, double ns_per_op) {
        std::fprintf(out, "%s,%s,%s,%.10g,%.6f\n", sweep, container, operation, x,
                     ns_per_op);
    }
};

// ---- timing helpers (best/minimum of `repeats` passes) -----------------

template <typename Map>
[[nodiscard]] double best_insert_ns_per_op(
    const std::vector<std::uint64_t>& keys, int repeats) {
    double best = 1e300;
    for (int r = 0; r < repeats; ++r) {
        Map map;
        const double start = now_ns();
        for (std::uint64_t key : keys) map[key] = key;
        const double elapsed = now_ns() - start;
        do_not_optimize(map);
        best = std::min(best, elapsed / static_cast<double>(keys.size()));
    }
    return best;
}

template <typename Map>
[[nodiscard]] double best_find_ns_per_op(const Map& map,
                                         const std::vector<std::uint64_t>& keys,
                                         int repeats) {
    double best = 1e300;
    for (int r = 0; r < repeats; ++r) {
        std::uint64_t checksum = 0;
        const double start = now_ns();
        for (std::uint64_t key : keys) {
            const auto it = map.find(key);
            if (it != map.end()) checksum += it->second;
        }
        const double elapsed = now_ns() - start;
        do_not_optimize(checksum);
        best = std::min(best, elapsed / static_cast<double>(keys.size()));
    }
    return best;
}

template <typename Map>
[[nodiscard]] double best_erase_ns_per_op(
    const std::vector<std::uint64_t>& keys, int repeats) {
    double best = 1e300;
    for (int r = 0; r < repeats; ++r) {
        Map map;
        for (std::uint64_t key : keys) map[key] = key;
        const double start = now_ns();
        for (std::uint64_t key : keys) map.erase(key);
        const double elapsed = now_ns() - start;
        do_not_optimize(map.size());
        best = std::min(best, elapsed / static_cast<double>(keys.size()));
    }
    return best;
}

template <typename Map>
[[nodiscard]] double best_iterate_ns_per_op(const Map& map, int repeats) {
    double best = 1e300;
    for (int r = 0; r < repeats; ++r) {
        std::uint64_t checksum = 0;
        const double start = now_ns();
        for (const auto& entry : map) checksum += entry.second;
        const double elapsed = now_ns() - start;
        do_not_optimize(checksum);
        best = std::min(best, elapsed / static_cast<double>(map.size()));
    }
    return best;
}

// Emit the full set of "size" sweep rows (insert / find-hit / find-miss /
// iterate / erase) for one container at one element count.
template <typename Map>
void run_size_point(Csv& csv, const char* container,
                    const std::vector<std::uint64_t>& present_keys,
                    const std::vector<std::uint64_t>& absent_keys,
                    const std::vector<std::uint64_t>& lookup_order, int repeats) {
    const double n = static_cast<double>(present_keys.size());

    csv.row("size", container, "insert", n,
            best_insert_ns_per_op<Map>(present_keys, repeats));

    Map map;
    for (std::uint64_t key : present_keys) map[key] = key;
    csv.row("size", container, "find_hit", n,
            best_find_ns_per_op(map, lookup_order, repeats));
    csv.row("size", container, "find_miss", n,
            best_find_ns_per_op(map, absent_keys, repeats));
    csv.row("size", container, "iterate", n,
            best_iterate_ns_per_op(map, repeats));
    csv.row("size", container, "erase", n,
            best_erase_ns_per_op<Map>(present_keys, repeats));
}

}  // namespace bench

#endif  // FUM_BENCH_COMMON_HPP
