// Exhaustive coverage of the std::unordered_map public interface, exercised on
// fum::unordered_map.  Every member that the standard requires is touched here.

#include <algorithm>
#include <string>
#include <type_traits>
#include <vector>

#include "fum/unordered_map.hpp"
#include "test_framework.hpp"
#include "test_types.hpp"

using fum_test::Tracked;

// --------------------------------------------------------------------------
// Member type sanity (compile-time interface conformance).
// --------------------------------------------------------------------------
TEST_CASE("member types match the standard") {
    using Map = fum::unordered_map<int, std::string>;
    static_assert(std::is_same_v<Map::key_type, int>);
    static_assert(std::is_same_v<Map::mapped_type, std::string>);
    static_assert(
        std::is_same_v<Map::value_type, std::pair<const int, std::string>>);
    static_assert(std::is_same_v<Map::size_type, std::size_t>);
    static_assert(std::is_same_v<Map::hasher, std::hash<int>>);
    static_assert(std::is_same_v<Map::key_equal, std::equal_to<int>>);
    static_assert(std::is_same_v<Map::reference, std::pair<const int, std::string>&>);
    static_assert(std::forward_iterator<Map::iterator>);
    static_assert(std::forward_iterator<Map::const_iterator>);
    CHECK(true);
}

// --------------------------------------------------------------------------
// Constructors
// --------------------------------------------------------------------------
TEST_CASE("default construction yields an empty map") {
    fum::unordered_map<int, int> map;
    CHECK(map.empty());
    CHECK(map.size() == 0);
    CHECK(map.begin() == map.end());
}

TEST_CASE("range constructor copies the input range") {
    std::vector<std::pair<int, int>> input{{1, 10}, {2, 20}, {3, 30}, {2, 99}};
    fum::unordered_map<int, int> map(input.begin(), input.end());
    CHECK(map.size() == 3);          // duplicate key 2 ignored (first wins)
    CHECK(map.at(2) == 20);
}

TEST_CASE("initializer-list constructor") {
    fum::unordered_map<int, std::string> map{{1, "a"}, {2, "b"}, {3, "c"}};
    CHECK(map.size() == 3);
    CHECK(map.at(2) == "b");
}

TEST_CASE("copy constructor produces an equal, independent map") {
    fum::unordered_map<int, int> original{{1, 1}, {2, 2}, {3, 3}};
    fum::unordered_map<int, int> copy(original);
    CHECK(copy == original);
    copy[1] = 99;
    CHECK(original.at(1) == 1);  // independence
}

TEST_CASE("move constructor transfers ownership") {
    fum::unordered_map<int, int> original{{1, 1}, {2, 2}, {3, 3}};
    fum::unordered_map<int, int> moved(std::move(original));
    CHECK(moved.size() == 3);
    CHECK(moved.at(2) == 2);
    CHECK(original.empty());  // NOLINT: intentionally inspecting moved-from
}

TEST_CASE("bucket-count constructor pre-sizes the table") {
    fum::unordered_map<int, int> map(100);
    CHECK(map.bucket_count() >= 100);
    CHECK(map.empty());
}

// --------------------------------------------------------------------------
// Assignment
// --------------------------------------------------------------------------
TEST_CASE("copy assignment") {
    fum::unordered_map<int, int> a{{1, 1}, {2, 2}};
    fum::unordered_map<int, int> b{{9, 9}};
    b = a;
    CHECK(b == a);
    CHECK(b.size() == 2);
}

TEST_CASE("move assignment") {
    fum::unordered_map<int, int> a{{1, 1}, {2, 2}};
    fum::unordered_map<int, int> b{{9, 9}};
    b = std::move(a);
    CHECK(b.size() == 2);
    CHECK(b.at(1) == 1);
}

TEST_CASE("initializer-list assignment") {
    fum::unordered_map<int, int> map{{5, 5}};
    map = {{1, 1}, {2, 2}, {3, 3}};
    CHECK(map.size() == 3);
    CHECK(!map.contains(5));
}

TEST_CASE("self copy-assignment is a no-op") {
    fum::unordered_map<int, int> map{{1, 1}, {2, 2}};
    auto& alias = map;
    map = alias;
    CHECK(map.size() == 2);
    CHECK(map.at(1) == 1);
}

// --------------------------------------------------------------------------
// Element access
// --------------------------------------------------------------------------
TEST_CASE("operator[] inserts default and returns reference") {
    fum::unordered_map<int, int> map;
    CHECK(map[42] == 0);  // value-initialised
    map[42] = 7;
    CHECK(map[42] == 7);
    CHECK(map.size() == 1);
}

TEST_CASE("at throws on missing key") {
    fum::unordered_map<int, int> map{{1, 1}};
    CHECK(map.at(1) == 1);
    CHECK_THROWS_AS(map.at(2), std::out_of_range);
    const auto& const_map = map;
    CHECK_THROWS_AS(const_map.at(2), std::out_of_range);
}

// --------------------------------------------------------------------------
// Modifiers
// --------------------------------------------------------------------------
TEST_CASE("insert reports whether the element was new") {
    fum::unordered_map<int, int> map;
    auto [it1, inserted1] = map.insert({1, 10});
    CHECK(inserted1);
    CHECK(it1->second == 10);
    auto [it2, inserted2] = map.insert({1, 20});
    CHECK(!inserted2);
    CHECK(it2->second == 10);  // existing value retained
}

TEST_CASE("insert range and initializer list") {
    fum::unordered_map<int, int> map;
    std::vector<std::pair<int, int>> data{{1, 1}, {2, 2}};
    map.insert(data.begin(), data.end());
    map.insert({{3, 3}, {4, 4}});
    CHECK(map.size() == 4);
}

TEST_CASE("emplace constructs in place") {
    fum::unordered_map<int, std::string> map;
    auto [it, inserted] = map.emplace(1, "hello");
    CHECK(inserted);
    CHECK(it->second == "hello");
    auto [it2, inserted2] = map.emplace(1, "world");
    CHECK(!inserted2);
    CHECK(it2->second == "hello");
}

TEST_CASE("try_emplace does not construct mapped for existing key") {
    fum::unordered_map<int, Tracked> map;
    map.try_emplace(1, 100);
    fum_test::live_counter().reset();
    auto [it, inserted] = map.try_emplace(1, 999);
    CHECK(!inserted);
    CHECK(it->second.value == 100);
    // No new Tracked should have been constructed for the failed try_emplace.
    CHECK(fum_test::live_counter().constructed == 0);
}

TEST_CASE("insert_or_assign overwrites existing mapped value") {
    fum::unordered_map<int, std::string> map;
    auto [it1, inserted1] = map.insert_or_assign(1, "a");
    CHECK(inserted1);
    auto [it2, inserted2] = map.insert_or_assign(1, "b");
    CHECK(!inserted2);
    CHECK(it2->second == "b");
}

TEST_CASE("erase by key, iterator, and range") {
    fum::unordered_map<int, int> map;
    for (int i = 0; i < 10; ++i) map[i] = i;
    CHECK(map.erase(5) == 1);
    CHECK(map.erase(5) == 0);
    CHECK(map.size() == 9);

    auto it = map.find(3);
    auto next = map.erase(it);
    CHECK(!map.contains(3));
    (void)next;

    // Erase everything via the canonical loop.
    for (auto cursor = map.begin(); cursor != map.end();) {
        cursor = map.erase(cursor);
    }
    CHECK(map.empty());
}

TEST_CASE("erase returns iterator usable to continue traversal") {
    fum::unordered_map<int, int> map;
    for (int i = 0; i < 100; ++i) map[i] = i;
    long visited = 0;
    for (auto it = map.begin(); it != map.end();) {
        if (it->first % 2 == 0) {
            it = map.erase(it);
        } else {
            ++it;
            ++visited;
        }
    }
    CHECK(map.size() == 50);
    CHECK(visited == 50);
    for (const auto& [k, v] : map) CHECK(k % 2 == 1);
}

TEST_CASE("clear empties without changing bucket_count semantics") {
    fum::unordered_map<int, int> map;
    for (int i = 0; i < 50; ++i) map[i] = i;
    const auto buckets = map.bucket_count();
    map.clear();
    CHECK(map.empty());
    CHECK(map.bucket_count() == buckets);
    map[1] = 1;  // reusable after clear
    CHECK(map.size() == 1);
}

TEST_CASE("swap exchanges contents") {
    fum::unordered_map<int, int> a{{1, 1}, {2, 2}};
    fum::unordered_map<int, int> b{{9, 9}};
    using std::swap;
    swap(a, b);
    CHECK(a.size() == 1);
    CHECK(a.at(9) == 9);
    CHECK(b.size() == 2);
}

// --------------------------------------------------------------------------
// Lookup
// --------------------------------------------------------------------------
TEST_CASE("find, count, contains, equal_range") {
    fum::unordered_map<int, int> map{{1, 1}, {2, 2}, {3, 3}};
    CHECK(map.find(2)->second == 2);
    CHECK(map.find(99) == map.end());
    CHECK(map.count(2) == 1);
    CHECK(map.count(99) == 0);
    CHECK(map.contains(3));
    CHECK(!map.contains(99));

    auto [first, last] = map.equal_range(2);
    CHECK(first != last);
    CHECK(first->first == 2);
    CHECK(++first == last);

    auto [mfirst, mlast] = map.equal_range(99);
    CHECK(mfirst == mlast);
}

// --------------------------------------------------------------------------
// Iterators
// --------------------------------------------------------------------------
TEST_CASE("iteration visits every element exactly once") {
    fum::unordered_map<int, int> map;
    for (int i = 0; i < 1000; ++i) map[i] = i * i;
    std::vector<bool> seen(1000, false);
    long count = 0;
    for (const auto& [k, v] : map) {
        CHECK(v == k * k);
        CHECK(!seen[k]);
        seen[k] = true;
        ++count;
    }
    CHECK(count == 1000);
    CHECK(std::all_of(seen.begin(), seen.end(), [](bool b) { return b; }));
}

TEST_CASE("const iteration and conversions") {
    fum::unordered_map<int, int> map{{1, 1}, {2, 2}};
    const auto& const_map = map;
    long sum = 0;
    for (auto it = const_map.cbegin(); it != const_map.cend(); ++it) {
        sum += it->second;
    }
    CHECK(sum == 3);
    // iterator -> const_iterator conversion compiles and compares.
    fum::unordered_map<int, int>::const_iterator ci = map.begin();
    CHECK(ci == map.cbegin());
}

// --------------------------------------------------------------------------
// Bucket / hash policy interface
// --------------------------------------------------------------------------
TEST_CASE("bucket interface is consistent") {
    fum::unordered_map<int, int> map;
    for (int i = 0; i < 100; ++i) map[i] = i;
    CHECK(map.bucket_count() > 0);
    CHECK(map.max_bucket_count() > map.bucket_count());

    // Every element is found within the bucket reported by bucket(key), and the
    // sum of bucket sizes equals size().
    std::size_t summed = 0;
    for (std::size_t b = 0; b < map.bucket_count(); ++b) {
        summed += map.bucket_size(b);
    }
    CHECK(summed == map.size());

    for (const auto& [k, v] : map) {
        const std::size_t home = map.bucket(k);
        bool found_in_bucket = false;
        for (auto it = map.begin(home); it != map.end(home); ++it) {
            if (it->first == k) found_in_bucket = true;
        }
        CHECK(found_in_bucket);
    }
}

TEST_CASE("load factor and rehash") {
    fum::unordered_map<int, int> map;
    CHECK(map.max_load_factor() > 0.0f);
    map.reserve(1000);
    const auto buckets_after_reserve = map.bucket_count();
    for (int i = 0; i < 700; ++i) map[i] = i;
    // reserve(1000) should have prevented any rehash for 700 insertions.
    CHECK(map.bucket_count() == buckets_after_reserve);
    CHECK(map.load_factor() <= map.max_load_factor());

    map.rehash(4096);
    CHECK(map.bucket_count() >= 4096);
    CHECK(map.size() == 700);  // rehash preserves elements
    for (int i = 0; i < 700; ++i) CHECK(map.at(i) == i);
}

TEST_CASE("observers return the stored function objects") {
    fum::unordered_map<int, int> map;
    auto hasher = map.hash_function();
    auto eq = map.key_eq();
    CHECK(hasher(5) == std::hash<int>{}(5));
    CHECK(eq(5, 5));
    CHECK(!eq(5, 6));
}

// --------------------------------------------------------------------------
// Non-member helpers
// --------------------------------------------------------------------------
TEST_CASE("operator== compares by content, ignoring order") {
    fum::unordered_map<int, int> a{{1, 1}, {2, 2}, {3, 3}};
    fum::unordered_map<int, int> b{{3, 3}, {2, 2}, {1, 1}};
    CHECK(a == b);
    b[2] = 99;
    CHECK(a != b);
    b.erase(2);
    CHECK(a != b);  // differing sizes
}

TEST_CASE("erase_if removes matching elements") {
    fum::unordered_map<int, int> map;
    for (int i = 0; i < 100; ++i) map[i] = i;
    const auto removed =
        fum::erase_if(map, [](const auto& kv) { return kv.first % 3 == 0; });
    CHECK(removed == 34);  // 0,3,...,99
    CHECK(map.size() == 66);
    for (const auto& [k, v] : map) CHECK(k % 3 != 0);
}

int main() { return fum_test::run_all(); }
