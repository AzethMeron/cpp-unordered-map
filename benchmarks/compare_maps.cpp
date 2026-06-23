// Four-way comparison: fum::unordered_map vs std::unordered_map vs
// boost::unordered_flat_map vs absl::flat_hash_map, over an element-count sweep
// (random uint64 keys).  Emits the same CSV schema as sweep.cpp:
//
//     sweep,container,operation,x,ns_per_op
//
// Boost and Abseil are optional: each is included only if its header is present
// (__has_include), so this file still builds with whichever subset is
// installed.  Because it pulls in third-party headers it is built with relaxed
// warnings (NOT the project's strict -Wconversion/-Werror set).
//
// Build (see scripts/run_comparison.sh for the full command):
//   g++ -std=c++20 -O3 -DNDEBUG -Iinclude -Ibenchmarks
//       benchmarks/compare_maps.cpp -o compare_maps  <absl link libs>
//   ./compare_maps results_compare.csv
//
// Context: boost/absl are *flat* maps (elements move on rehash) and so are NOT
// drop-in replacements for std::unordered_map — they break the pointer/reference
// stability that fum preserves.  This benchmark measures the speed fum gives up
// (if any) to keep that guarantee.

#include <cstdint>
#include <cstdio>
#include <random>
#include <unordered_map>
#include <vector>

#include "bench_common.hpp"
#include "fum/unordered_map.hpp"

#if __has_include(<boost/unordered/unordered_flat_map.hpp>)
#include <boost/unordered/unordered_flat_map.hpp>
#define FUM_HAVE_BOOST 1
#endif

#if __has_include(<absl/container/flat_hash_map.h>)
#include <absl/container/flat_hash_map.h>
#define FUM_HAVE_ABSL 1
#endif

namespace {

using bench::Csv;
using bench::make_distinct_keys;
using bench::run_size_point;

template <template <typename...> class Map>
void run_all_sizes(Csv& csv, const char* container) {
    const std::vector<std::size_t> sizes = {
        1000,   2000,   5000,    10000,   20000,   50000,
        100000, 200000, 500000,  1000000, 2000000, 4000000};
    for (std::size_t n : sizes) {
        const int repeats = n <= 20000 ? 50 : (n <= 200000 ? 8 : 3);
        const std::vector<std::uint64_t> present = make_distinct_keys(n, 1);
        const std::vector<std::uint64_t> absent = make_distinct_keys(n, 2);
        std::vector<std::uint64_t> lookup_order = present;
        std::mt19937_64 rng(3);
        std::shuffle(lookup_order.begin(), lookup_order.end(), rng);
        run_size_point<Map<std::uint64_t, std::uint64_t>>(
            csv, container, present, absent, lookup_order, repeats);
        std::fprintf(stderr, "%s: N=%zu done\n", container, n);
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
    Csv csv{out};
    std::fprintf(out, "sweep,container,operation,x,ns_per_op\n");

    run_all_sizes<std::unordered_map>(csv, "std");
    run_all_sizes<fum::unordered_map>(csv, "fum");
#ifdef FUM_HAVE_BOOST
    run_all_sizes<boost::unordered_flat_map>(csv, "boost");
#else
    std::fprintf(stderr, "boost::unordered_flat_map not available; skipped\n");
#endif
#ifdef FUM_HAVE_ABSL
    run_all_sizes<absl::flat_hash_map>(csv, "absl");
#else
    std::fprintf(stderr, "absl::flat_hash_map not available; skipped\n");
#endif

    if (out != stdout) std::fclose(out);
    std::fprintf(stderr, "comparison complete\n");
    return 0;
}
