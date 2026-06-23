// Coverage of the std::unordered_set public interface on fum::unordered_set,
// including set-specific semantics (constant iterators, value()-style node
// handles), RAII/leak checks, adversarial keys and pointer stability.

#include <algorithm>
#include <string>
#include <type_traits>
#include <unordered_set>
#include <vector>

#include "fum/unordered_set.hpp"
#include "test_framework.hpp"
#include "test_types.hpp"

using fum_test::BadHash;
using fum_test::live_counter;
using fum_test::Tracked;

// --------------------------------------------------------------------------
// Member types and constant-iterator guarantee.
// --------------------------------------------------------------------------
TEST_CASE("set member types match the standard") {
    using Set = fum::unordered_set<std::string>;
    static_assert(std::is_same_v<Set::key_type, std::string>);
    static_assert(std::is_same_v<Set::value_type, std::string>);
    static_assert(std::is_same_v<Set::hasher, std::hash<std::string>>);
    static_assert(std::forward_iterator<Set::iterator>);
    // Elements are immutable through any iterator.
    static_assert(
        std::is_same_v<decltype(*std::declval<Set::iterator>()), const std::string&>);
    static_assert(
        std::is_same_v<decltype(*std::declval<Set::const_iterator>()),
                       const std::string&>);
    CHECK(true);
}

// --------------------------------------------------------------------------
// Construction
// --------------------------------------------------------------------------
TEST_CASE("set default construction is empty") {
    fum::unordered_set<int> set;
    CHECK(set.empty());
    CHECK(set.size() == 0);
    CHECK(set.begin() == set.end());
}

TEST_CASE("set range and initializer-list construction deduplicate") {
    std::vector<int> input{1, 2, 3, 2, 1, 4};
    fum::unordered_set<int> from_range(input.begin(), input.end());
    CHECK(from_range.size() == 4);
    fum::unordered_set<int> from_init{5, 5, 6, 7, 7};
    CHECK(from_init.size() == 3);
}

TEST_CASE("set CTAD deduces the key type") {
    fum::unordered_set deduced{1, 2, 3};
    static_assert(std::is_same_v<decltype(deduced)::key_type, int>);
    CHECK(deduced.size() == 3);
    std::vector<std::string> words{"a", "b"};
    fum::unordered_set from_iters(words.begin(), words.end());
    static_assert(std::is_same_v<decltype(from_iters)::key_type, std::string>);
    CHECK(from_iters.size() == 2);
}

TEST_CASE("set copy and move") {
    fum::unordered_set<int> original{1, 2, 3};
    fum::unordered_set<int> copy(original);
    CHECK(copy == original);
    copy.insert(4);
    CHECK(!original.contains(4));  // independence
    fum::unordered_set<int> moved(std::move(copy));
    CHECK(moved.size() == 4);
}

// --------------------------------------------------------------------------
// Modifiers
// --------------------------------------------------------------------------
TEST_CASE("set insert reports novelty and rejects duplicates") {
    fum::unordered_set<int> set;
    auto [it1, ok1] = set.insert(42);
    CHECK(ok1);
    CHECK(*it1 == 42);
    auto [it2, ok2] = set.insert(42);
    CHECK(!ok2);
    CHECK(*it2 == 42);
    CHECK(set.size() == 1);
}

TEST_CASE("set emplace and range insert") {
    fum::unordered_set<std::string> set;
    set.emplace("hello");
    set.emplace(5, 'x');  // constructs "xxxxx" in place
    CHECK(set.contains("hello"));
    CHECK(set.contains("xxxxx"));
    std::vector<std::string> more{"a", "b", "a"};
    set.insert(more.begin(), more.end());
    CHECK(set.size() == 4);
}

TEST_CASE("set erase by key, iterator, range") {
    fum::unordered_set<int> set;
    for (int i = 0; i < 10; ++i) set.insert(i);
    CHECK(set.erase(5) == 1);
    CHECK(set.erase(5) == 0);
    CHECK(set.size() == 9);
    auto it = set.find(3);
    set.erase(it);
    CHECK(!set.contains(3));
    for (auto cursor = set.begin(); cursor != set.end();) {
        cursor = set.erase(cursor);
    }
    CHECK(set.empty());
}

TEST_CASE("set clear and swap") {
    fum::unordered_set<int> a{1, 2, 3};
    fum::unordered_set<int> b{9};
    using std::swap;
    swap(a, b);
    CHECK(a.size() == 1);
    CHECK(a.contains(9));
    CHECK(b.size() == 3);
    b.clear();
    CHECK(b.empty());
}

// --------------------------------------------------------------------------
// Lookup
// --------------------------------------------------------------------------
TEST_CASE("set find, count, contains, equal_range") {
    fum::unordered_set<int> set{1, 2, 3};
    CHECK(*set.find(2) == 2);
    CHECK(set.find(99) == set.end());
    CHECK(set.count(2) == 1);
    CHECK(set.count(99) == 0);
    CHECK(set.contains(3));
    auto [first, last] = set.equal_range(2);
    CHECK(first != last);
    CHECK(*first == 2);
    CHECK(++first == last);
    auto [mf, ml] = set.equal_range(99);
    CHECK(mf == ml);
}

// --------------------------------------------------------------------------
// Iteration
// --------------------------------------------------------------------------
TEST_CASE("set iterates every element exactly once") {
    fum::unordered_set<int> set;
    for (int i = 0; i < 1000; ++i) set.insert(i);
    std::vector<bool> seen(1000, false);
    long count = 0;
    for (const int value : set) {
        CHECK(!seen[value]);
        seen[value] = true;
        ++count;
    }
    CHECK(count == 1000);
    CHECK(std::all_of(seen.begin(), seen.end(), [](bool b) { return b; }));
}

// --------------------------------------------------------------------------
// Node handles
// --------------------------------------------------------------------------
TEST_CASE("set extract yields a value()-bearing handle") {
    fum::unordered_set<int> set{1, 2, 3};
    auto node = set.extract(2);
    CHECK(!node.empty());
    CHECK(node.value() == 2);
    CHECK(set.size() == 2);
    CHECK(!set.contains(2));
    auto missing = set.extract(99);
    CHECK(missing.empty());
}

TEST_CASE("set node handle can be mutated and reinserted") {
    fum::unordered_set<int> set{1, 2, 3};
    auto node = set.extract(1);
    node.value() = 100;
    auto result = set.insert(std::move(node));
    CHECK(result.inserted);
    CHECK(*result.position == 100);
    CHECK(set.contains(100));
    CHECK(!set.contains(1));

    // Re-inserting a duplicate returns the node to the caller.
    auto dup = set.extract(2);
    dup.value() = 3;  // 3 already present
    auto rejected = set.insert(std::move(dup));
    CHECK(!rejected.inserted);
    CHECK(!rejected.node.empty());
    CHECK(rejected.node.value() == 3);
}

TEST_CASE("set merge moves non-conflicting elements") {
    fum::unordered_set<int> dest{1, 2};
    fum::unordered_set<int> source{2, 3, 4};
    dest.merge(source);
    CHECK(dest.size() == 4);
    CHECK(dest.contains(3));
    CHECK(dest.contains(4));
    CHECK(source.size() == 1);
    CHECK(source.contains(2));  // conflict stays in source
}

// --------------------------------------------------------------------------
// Bucket / hash policy
// --------------------------------------------------------------------------
TEST_CASE("set bucket interface is consistent") {
    fum::unordered_set<int> set;
    for (int i = 0; i < 100; ++i) set.insert(i);
    std::size_t summed = 0;
    for (std::size_t b = 0; b < set.bucket_count(); ++b) {
        summed += set.bucket_size(b);
    }
    CHECK(summed == set.size());
    for (const int value : set) {
        const std::size_t home = set.bucket(value);
        bool found = false;
        for (auto it = set.begin(home); it != set.end(home); ++it) {
            if (*it == value) found = true;
        }
        CHECK(found);
    }
}

TEST_CASE("set reserve and rehash preserve elements") {
    fum::unordered_set<int> set;
    set.reserve(1000);
    const auto buckets = set.bucket_count();
    for (int i = 0; i < 700; ++i) set.insert(i);
    CHECK(set.bucket_count() == buckets);  // no rehash within reserved capacity
    set.rehash(8192);
    CHECK(set.bucket_count() >= 8192);
    CHECK(set.size() == 700);
    for (int i = 0; i < 700; ++i) CHECK(set.contains(i));
}

// --------------------------------------------------------------------------
// RAII / leaks
// --------------------------------------------------------------------------
TEST_CASE("set destroys every element exactly once") {
    live_counter().reset();
    {
        fum::unordered_set<Tracked> set;
        for (int i = 0; i < 1000; ++i) set.emplace(i);
        CHECK(set.size() == 1000);
        for (int i = 0; i < 500; ++i) set.erase(Tracked(i));
        CHECK(set.size() == 500);
    }
    CHECK(live_counter().alive == 0);
}

TEST_CASE("set free-list reuse over many insert/erase cycles") {
    live_counter().reset();
    {
        fum::unordered_set<Tracked> set;
        for (int cycle = 0; cycle < 100; ++cycle) {
            for (int i = 0; i < 100; ++i) set.emplace(i);
            for (int i = 0; i < 100; ++i) set.erase(Tracked(i));
            CHECK(set.empty());
        }
    }
    CHECK(live_counter().alive == 0);
}

// --------------------------------------------------------------------------
// Adversarial / pointer stability
// --------------------------------------------------------------------------
TEST_CASE("set correctness under all-keys-collide hash") {
    fum::unordered_set<int, BadHash> set;
    for (int i = 0; i < 3000; ++i) set.insert(i);
    CHECK(set.size() == 3000);
    for (int i = 0; i < 3000; ++i) REQUIRE(set.contains(i));
    for (int i = 0; i < 3000; i += 2) set.erase(i);
    CHECK(set.size() == 1500);
    for (int i = 1; i < 3000; i += 2) REQUIRE(set.contains(i));
}

TEST_CASE("set keeps element addresses stable across rehash") {
    fum::unordered_set<int> set;
    set.insert(7);
    const int* address = &*set.find(7);
    for (int i = 100; i < 100000; ++i) set.insert(i);
    CHECK(&*set.find(7) == address);
}

TEST_CASE("set erase_if and operator==") {
    fum::unordered_set<int> set;
    for (int i = 0; i < 100; ++i) set.insert(i);
    const auto removed = fum::erase_if(set, [](int v) { return v % 4 == 0; });
    CHECK(removed == 25);
    CHECK(set.size() == 75);
    for (const int v : set) CHECK(v % 4 != 0);

    fum::unordered_set<int> a{1, 2, 3};
    fum::unordered_set<int> b{3, 1, 2};
    CHECK(a == b);
    b.insert(4);
    CHECK(a != b);
}

int main() { return fum_test::run_all(); }
