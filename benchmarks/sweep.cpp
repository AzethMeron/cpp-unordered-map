// Parameter-sweep benchmark for fum::unordered_map vs std::unordered_map.
//
// Emits CSV (to the path in argv[1], or stdout) with one row per measurement:
//
//     sweep,container,operation,x,ns_per_op
//
//   * sweep=size    : x = element count N (random keys), every core operation.
//   * sweep=density : x = achieved load factor, at a fixed table size, showing
//                     how each container degrades as it fills up.
//
// Build (matches the project's strict flag set):
//   g++ -std=c++20 -O3 -DNDEBUG -Iinclude benchmarks/sweep.cpp -o sweep
//   ./sweep results.csv
//
// Timing reports the best (minimum) of several repeats to suppress noise.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

#include "bench_common.hpp"
#include "fum/unordered_map.hpp"

namespace {

using bench::best_find_ns_per_op;  // also used by the density sweep below
using bench::Csv;
using bench::make_distinct_keys;
using bench::run_size_point;

// ---- size sweep --------------------------------------------------------

void size_sweep(Csv& csv) {
    const std::vector<std::size_t> sizes = {
        1000,    2000,    5000,    10000,   20000,    50000,
        100000,  200000,  500000,  1000000, 2000000,  4000000};
    for (std::size_t n : sizes) {
        // More repeats when N is small (cheap) for stability; fewer when large.
        const int repeats = n <= 20000 ? 50 : (n <= 200000 ? 8 : 3);
        const std::vector<std::uint64_t> present = make_distinct_keys(n, 1);
        const std::vector<std::uint64_t> absent = make_distinct_keys(n, 2);
        std::vector<std::uint64_t> lookup_order = present;
        std::mt19937_64 rng(3);
        std::shuffle(lookup_order.begin(), lookup_order.end(), rng);

        run_size_point<std::unordered_map<std::uint64_t, std::uint64_t>>(
            csv, "std", present, absent, lookup_order, repeats);
        run_size_point<fum::unordered_map<std::uint64_t, std::uint64_t>>(
            csv, "fum", present, absent, lookup_order, repeats);

        std::fprintf(stderr, "size sweep: N=%zu done\n", n);
    }
}

// ---- density sweep -----------------------------------------------------
// Hold the table size fixed and vary how full it is, so the x-axis is the
// achieved load factor.  We pin the bucket count with rehash() and raise
// max_load_factor so the fill never triggers an internal rehash mid-experiment.

template <typename Map>
void run_density_point(Csv& csv, const char* container,
                       const std::vector<std::uint64_t>& all_keys,
                       std::size_t bucket_count, std::size_t fill_count,
                       int repeats) {
    Map map;
    map.max_load_factor(0.99f);
    map.rehash(bucket_count);  // pin the table size for the whole sweep
    for (std::size_t i = 0; i < fill_count; ++i) map[all_keys[i]] = all_keys[i];

    std::vector<std::uint64_t> lookup(all_keys.begin(),
                                      all_keys.begin() +
                                          static_cast<std::ptrdiff_t>(fill_count));
    std::mt19937_64 rng(9);
    std::shuffle(lookup.begin(), lookup.end(), rng);

    const double load = static_cast<double>(map.load_factor());
    csv.row("density", container, "find_hit", load,
            best_find_ns_per_op(map, lookup, repeats));
    csv.row("density", container, "find_miss", load,
            best_find_ns_per_op(map, make_distinct_keys(fill_count, 99), repeats));
}

void density_sweep(Csv& csv) {
    // Fixed power-of-two table; fill a fraction of it for each density point.
    const std::size_t bucket_count = 1u << 20;  // ~1.05M slots
    const std::size_t max_fill =
        static_cast<std::size_t>(0.95 * static_cast<double>(bucket_count));
    const std::vector<std::uint64_t> all_keys = make_distinct_keys(max_fill, 5);
    for (double density = 0.1; density <= 0.951; density += 0.05) {
        const std::size_t fill = static_cast<std::size_t>(
            density * static_cast<double>(bucket_count));
        run_density_point<std::unordered_map<std::uint64_t, std::uint64_t>>(
            csv, "std", all_keys, bucket_count, fill, 5);
        run_density_point<fum::unordered_map<std::uint64_t, std::uint64_t>>(
            csv, "fum", all_keys, bucket_count, fill, 5);
        std::fprintf(stderr, "density sweep: target=%.2f done\n", density);
    }
}

}  // namespace

int main(int argc, char** argv) {
    std::FILE* out = stdout;
    if (argc > 1) {
        out = std::fopen(argv[1], "w");
        if (out == nullptr) {
            std::fprintf(stderr, "cannot open %s\n", argv[1]);
            return 1;
        }
    }
    // Optional mode in argv[2]: "size" | "density" | "all" (default "all").
    const std::string mode = argc > 2 ? argv[2] : "all";
    Csv csv{out};
    std::fprintf(out, "sweep,container,operation,x,ns_per_op\n");
    if (mode == "all" || mode == "size") size_sweep(csv);
    if (mode == "all" || mode == "density") density_sweep(csv);
    if (out != stdout) std::fclose(out);
    std::fprintf(stderr, "sweep complete\n");
    return 0;
}
