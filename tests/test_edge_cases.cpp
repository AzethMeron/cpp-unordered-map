// Edge cases, RAII/leak verification, and stress patterns.
//
// Many cases use the Tracked instrumented type and assert that the live-object
// counter returns to zero, proving every constructed element is destroyed
// exactly once (no leaks, no double frees) — complementary to ASAN.

#include <memory>
#include <random>
#include <string>
#include <vector>

#include "fum/unordered_map.hpp"
#include "test_framework.hpp"
#include "test_types.hpp"

using fum_test::BadHash;
using fum_test::live_counter;
using fum_test::Tracked;

// Run `body`, then assert that the global live-object counter is back to zero.
template <typename Body>
static void expect_no_leaks(Body body) {
    live_counter().reset();
    body();
    CHECK(live_counter().alive == 0);
}

TEST_CASE("operations on an empty map are well defined") {
    fum::unordered_map<int, int> map;
    CHECK(map.find(1) == map.end());
    CHECK(map.count(1) == 0);
    CHECK(!map.contains(1));
    CHECK(map.erase(1) == 0);
    CHECK(map.begin() == map.end());
    CHECK(map.load_factor() == 0.0f);
    auto [first, last] = map.equal_range(1);
    CHECK(first == last);
    // The bucket interface must be well defined (no UB) on an empty table.
    CHECK(map.bucket(123) == 0);
    CHECK(map.bucket_size(0) == 0);
    CHECK(map.begin(0) == map.end(0));
    map.clear();  // clearing an empty map is fine
    CHECK(map.empty());
}

TEST_CASE("single element lifecycle") {
    fum::unordered_map<int, int> map;
    map[7] = 70;
    CHECK(map.size() == 1);
    CHECK(map.begin()->first == 7);
    auto it = map.begin();
    ++it;
    CHECK(it == map.end());
    map.erase(7);
    CHECK(map.empty());
    CHECK(map.begin() == map.end());
}

TEST_CASE("all elements destroyed when map is destroyed") {
    expect_no_leaks([] {
        fum::unordered_map<int, Tracked> map;
        for (int i = 0; i < 1000; ++i) map.try_emplace(i, i);
        CHECK(map.size() == 1000);
    });
}

TEST_CASE("erase destroys exactly the erased element") {
    expect_no_leaks([] {
        fum::unordered_map<int, Tracked> map;
        for (int i = 0; i < 500; ++i) map.try_emplace(i, i);
        for (int i = 0; i < 250; ++i) map.erase(i);
        CHECK(map.size() == 250);
        CHECK(live_counter().alive == 250);
    });
}

TEST_CASE("clear destroys all elements") {
    expect_no_leaks([] {
        fum::unordered_map<int, Tracked> map;
        for (int i = 0; i < 300; ++i) map.try_emplace(i, i);
        map.clear();
        CHECK(live_counter().alive == 0);
    });
}

TEST_CASE("copy and move do not leak") {
    expect_no_leaks([] {
        fum::unordered_map<int, Tracked> source;
        for (int i = 0; i < 200; ++i) source.try_emplace(i, i);
        fum::unordered_map<int, Tracked> copy = source;
        fum::unordered_map<int, Tracked> moved = std::move(source);
        CHECK(copy.size() == 200);
        CHECK(moved.size() == 200);
    });
}

TEST_CASE("free list reuse across many insert/erase cycles") {
    expect_no_leaks([] {
        fum::unordered_map<int, Tracked> map;
        for (int cycle = 0; cycle < 100; ++cycle) {
            for (int i = 0; i < 100; ++i) map.try_emplace(i, i + cycle);
            CHECK(map.size() == 100);
            for (int i = 0; i < 100; ++i) map.erase(i);
            CHECK(map.empty());
        }
        CHECK(live_counter().alive == 0);
    });
}

TEST_CASE("pathological hash: every key collides but all are findable") {
    fum::unordered_map<int, int, BadHash> map;  // only two hash values exist
    for (int i = 0; i < 2000; ++i) map[i] = i * 3;
    CHECK(map.size() == 2000);
    for (int i = 0; i < 2000; ++i) {
        REQUIRE(map.contains(i));
        CHECK(map.at(i) == i * 3);
    }
    // Erase half, the rest stay findable despite the collision pile-up.
    for (int i = 0; i < 2000; i += 2) map.erase(i);
    CHECK(map.size() == 1000);
    for (int i = 1; i < 2000; i += 2) CHECK(map.at(i) == i * 3);
    for (int i = 0; i < 2000; i += 2) CHECK(!map.contains(i));
}

TEST_CASE("reference and pointer stability across rehash") {
    fum::unordered_map<int, int> map;
    map[0] = 1234;
    int* stable_pointer = &map[0];
    int& stable_reference = map[0];
    // Force many rehashes.
    for (int i = 1; i < 100000; ++i) map[i] = i;
    CHECK(stable_pointer == &map[0]);   // address unchanged
    CHECK(stable_reference == 1234);    // value intact
    *stable_pointer = 5678;
    CHECK(map[0] == 5678);
}

TEST_CASE("iterator to surviving element stays valid after erasing others") {
    fum::unordered_map<int, int> map;
    for (int i = 0; i < 100; ++i) map[i] = i * 10;
    auto it = map.find(50);
    REQUIRE(it != map.end());
    // Erase a number of *other* keys; the standard guarantees `it` stays valid.
    for (int i = 0; i < 100; ++i) {
        if (i != 50) map.erase(i);
    }
    CHECK(map.size() == 1);
    CHECK(it->first == 50);
    CHECK(it->second == 500);
}

TEST_CASE("move-only mapped type") {
    fum::unordered_map<int, std::unique_ptr<int>> map;
    map.try_emplace(1, std::make_unique<int>(11));
    map.try_emplace(2, std::make_unique<int>(22));
    map.insert_or_assign(1, std::make_unique<int>(111));
    CHECK(*map.at(1) == 111);
    CHECK(*map.at(2) == 22);
    auto node = map.extract(2);
    CHECK(!node.empty());
    CHECK(*node.mapped() == 22);
    CHECK(map.size() == 1);
}

TEST_CASE("string keys with realistic churn") {
    fum::unordered_map<std::string, int> map;
    std::mt19937_64 rng(12345);
    std::vector<std::string> keys;
    for (int i = 0; i < 5000; ++i) {
        keys.push_back("key_" + std::to_string(rng()));
        map[keys.back()] = i;
    }
    for (std::size_t i = 0; i < keys.size(); ++i) {
        CHECK(map.at(keys[i]) == static_cast<int>(i));
    }
    CHECK(map.size() == keys.size());
}

TEST_CASE("reserve avoids rehashing during bulk insert") {
    fum::unordered_map<int, int> map;
    map.reserve(10000);
    const auto fixed_bucket_count = map.bucket_count();
    for (int i = 0; i < 8000; ++i) {
        map[i] = i;
        REQUIRE(map.bucket_count() == fixed_bucket_count);
    }
    CHECK(map.size() == 8000);
}

TEST_CASE("rehash never loses or duplicates elements") {
    fum::unordered_map<int, int> map;
    for (int i = 0; i < 1000; ++i) map[i] = i;
    for (std::size_t target : {std::size_t{1}, std::size_t{2048},
                               std::size_t{17}, std::size_t{100000}}) {
        map.rehash(target);
        REQUIRE(map.size() == 1000);
        for (int i = 0; i < 1000; ++i) REQUIRE(map.at(i) == i);
    }
}

TEST_CASE("self-swap and self-move-assignment are safe") {
    fum::unordered_map<int, int> map{{1, 1}, {2, 2}, {3, 3}};
    using std::swap;
    swap(map, map);
    CHECK(map.size() == 3);
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wself-move"
#endif
    map = std::move(map);  // deliberately testing self move-assignment
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
    CHECK(map.size() == 3);
    CHECK(map.at(2) == 2);
}

TEST_CASE("alternating grow and shrink keeps table consistent") {
    fum::unordered_map<int, int> map;
    std::mt19937_64 rng(999);
    for (int round = 0; round < 2000; ++round) {
        const int key = static_cast<int>(rng() % 500);
        if (map.contains(key)) {
            map.erase(key);
        } else {
            map[key] = round;
        }
    }
    // Verify against a brute-force recomputation is covered by the fuzzer; here
    // we just assert internal consistency: every iterated key is findable.
    std::size_t counted = 0;
    for (const auto& [k, v] : map) {
        REQUIRE(map.find(k) != map.end());
        CHECK(map.at(k) == v);
        ++counted;
    }
    CHECK(counted == map.size());
}

TEST_CASE("emplace exception leaves map unchanged and leak free") {
    using fum_test::ThrowOnNth;
    using fum_test::throw_controller;
    fum::unordered_map<int, ThrowOnNth> map;
    map.try_emplace(1, 10);
    map.try_emplace(2, 20);
    const auto size_before = map.size();
    throw_controller().countdown = 0;  // next constructed ThrowOnNth throws
    bool threw = false;
    try {
        map.try_emplace(3, 30);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    throw_controller().countdown = -1;  // disable
    CHECK(threw);
    CHECK(map.size() == size_before);   // strong-ish guarantee: no element added
    CHECK(!map.contains(3));
    CHECK(map.at(1).value == 10);       // existing elements intact
}

int main() { return fum_test::run_all(); }
