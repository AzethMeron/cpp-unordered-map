// Adversarial tests.
//
// Open-addressing tables are notoriously vulnerable to crafted key patterns
// (e.g. the classic "keys that are multiples of the table size all share a home
// slot" attack against an identity hash).  These tests prove that:
//   * the bit-mixing layer spreads structured keys across the table, and
//   * correctness is preserved even under maximal, intentional collisions.
// A best-effort timing guard catches catastrophic (super-linear) regressions.

#include <chrono>
#include <cstdint>
#include <random>
#include <unordered_set>
#include <vector>

#include "fum/unordered_map.hpp"
#include "test_framework.hpp"
#include "test_types.hpp"

using fum_test::BadHash;
using fum_test::IdentityHash;

// With the mixing layer, keys forming an arithmetic progression with a large
// power-of-two stride must NOT collapse onto a handful of home buckets.
TEST_CASE("mixing spreads power-of-two-stride keys across buckets") {
    fum::unordered_map<std::size_t, int, IdentityHash> map;
    map.reserve(4096);  // fix the table so bucket() is meaningful
    const std::size_t stride = map.bucket_count();  // worst case for identity hash
    std::unordered_set<std::size_t> distinct_home_buckets;
    for (std::size_t i = 0; i < 256; ++i) {
        const std::size_t key = i * stride;  // 0, n, 2n, ... -> identical low bits
        map[key] = static_cast<int>(i);
        distinct_home_buckets.insert(map.bucket(key));
    }
    // Without mixing every one of these keys would map to bucket 0.  With
    // mixing we expect them spread across most of the probed buckets.
    CHECK(distinct_home_buckets.size() > 200);
    // And of course every key must still be retrievable.
    for (std::size_t i = 0; i < 256; ++i) {
        CHECK(map.at(i * stride) == static_cast<int>(i));
    }
}

TEST_CASE("correctness under multiples-of-power-of-two key attack") {
    fum::unordered_map<std::size_t, std::size_t, IdentityHash> map;
    constexpr std::size_t count = 50000;
    constexpr std::size_t stride = 1u << 16;  // collide hard under identity hash
    for (std::size_t i = 0; i < count; ++i) map[i * stride] = i;
    CHECK(map.size() == count);
    for (std::size_t i = 0; i < count; ++i) {
        REQUIRE(map.at(i * stride) == i);
    }
    // Erase the even multiples; odd ones remain intact.
    for (std::size_t i = 0; i < count; i += 2) map.erase(i * stride);
    CHECK(map.size() == count / 2);
    for (std::size_t i = 1; i < count; i += 2) REQUIRE(map.at(i * stride) == i);
}

TEST_CASE("correctness under all-keys-collide hash") {
    // Every key hashes to one of two values; the table degenerates to long
    // probe runs but must remain perfectly correct.
    fum::unordered_map<int, int, BadHash> map;
    constexpr int count = 4000;
    for (int i = 0; i < count; ++i) map[i] = i * 7;
    for (int i = 0; i < count; ++i) REQUIRE(map.at(i) == i * 7);

    // Heavy churn on the degenerate table.
    std::mt19937_64 rng(2024);
    for (int round = 0; round < 20000; ++round) {
        const int key = static_cast<int>(rng() % count);
        if (map.contains(key)) {
            map.erase(key);
        } else {
            map[key] = key * 7;
        }
    }
    for (const auto& [k, v] : map) {
        REQUIRE(map.find(k) != map.end());
        CHECK(v == k * 7);
    }
}

// Best-effort performance guard: an adversarial pattern must not be dramatically
// slower than random keys.  A removed/broken mixing layer would turn this into
// a super-linear blow-up and trip the (very generous) bound.
TEST_CASE("adversarial keys are not catastrophically slower than random") {
    constexpr std::size_t count = 200000;
    constexpr std::size_t stride = 1u << 14;

    auto time_run = [](const std::vector<std::size_t>& keys) {
        const auto start = std::chrono::steady_clock::now();
        fum::unordered_map<std::size_t, std::size_t, IdentityHash> map;
        std::size_t checksum = 0;
        for (std::size_t k : keys) map[k] = k;
        for (std::size_t k : keys) checksum += map.at(k);
        const auto end = std::chrono::steady_clock::now();
        // Prevent the work from being optimised away.
        volatile std::size_t sink = checksum;
        (void)sink;
        return std::chrono::duration<double>(end - start).count();
    };

    std::vector<std::size_t> adversarial;
    std::vector<std::size_t> random_keys;
    adversarial.reserve(count);
    random_keys.reserve(count);
    std::mt19937_64 rng(7);
    for (std::size_t i = 0; i < count; ++i) {
        adversarial.push_back(i * stride);
        random_keys.push_back(rng());
    }

    const double random_seconds = time_run(random_keys);
    const double adversarial_seconds = time_run(adversarial);

    // Extremely generous bound to avoid flakiness while still catching a true
    // quadratic regression (which would be hundreds of times slower).
    if (random_seconds > 1e-4) {  // only assert when the baseline is measurable
        CHECK(adversarial_seconds < random_seconds * 30.0);
    }
    CHECK(true);  // always at least exercise the path
}

int main() { return fum_test::run_all(); }
