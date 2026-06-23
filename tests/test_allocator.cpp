// Allocator-awareness tests: stateful allocator propagation (POCCA/POCMA/POCS),
// no-leak accounting, and get_allocator().

#include <string>

#include "fum/unordered_map.hpp"
#include "test_framework.hpp"
#include "test_types.hpp"

using fum_test::allocation_stats;
using fum_test::CountingAllocator;

template <typename Key, typename T>
using TrackedMap =
    fum::unordered_map<Key, T, std::hash<Key>, std::equal_to<Key>,
                       CountingAllocator<std::pair<const Key, T>>>;

TEST_CASE("custom allocator releases all memory it allocated") {
    const auto bytes_before = allocation_stats().outstanding_bytes;
    {
        TrackedMap<int, int> map(CountingAllocator<std::pair<const int, int>>(7));
        for (int i = 0; i < 5000; ++i) map[i] = i;
        CHECK(map.size() == 5000);
        CHECK(allocation_stats().outstanding_bytes > bytes_before);
        CHECK(map.get_allocator().id == 7);
    }
    CHECK(allocation_stats().outstanding_bytes == bytes_before);
}

TEST_CASE("allocator is propagated on copy construction by default selection") {
    // std::allocator-style: select_on_container_copy_construction returns a copy
    // of the same allocator. Our CountingAllocator does not customise it, so the
    // copy keeps the same id.
    TrackedMap<int, int> source(
        CountingAllocator<std::pair<const int, int>>(42));
    source[1] = 1;
    TrackedMap<int, int> copy = source;
    CHECK(copy.get_allocator().id == 42);
    CHECK(copy.at(1) == 1);
}

TEST_CASE("POCCA: copy assignment propagates the source allocator") {
    TrackedMap<int, int> source(CountingAllocator<std::pair<const int, int>>(1));
    TrackedMap<int, int> dest(CountingAllocator<std::pair<const int, int>>(2));
    for (int i = 0; i < 100; ++i) source[i] = i;
    dest[999] = 999;
    dest = source;
    CHECK(dest.get_allocator().id == 1);  // propagated
    CHECK(dest.size() == 100);
    CHECK(dest.at(50) == 50);
    CHECK(!dest.contains(999));
}

TEST_CASE("POCMA: move assignment propagates the source allocator") {
    TrackedMap<int, int> source(CountingAllocator<std::pair<const int, int>>(3));
    TrackedMap<int, int> dest(CountingAllocator<std::pair<const int, int>>(4));
    for (int i = 0; i < 100; ++i) source[i] = i * 2;
    dest = std::move(source);
    CHECK(dest.get_allocator().id == 3);  // propagated
    CHECK(dest.size() == 100);
    CHECK(dest.at(50) == 100);
}

TEST_CASE("move assignment between equal allocators steals storage") {
    TrackedMap<int, int> source(CountingAllocator<std::pair<const int, int>>(9));
    TrackedMap<int, int> dest(CountingAllocator<std::pair<const int, int>>(9));
    for (int i = 0; i < 1000; ++i) source[i] = i;
    const auto allocations_before = allocation_stats().total_allocations;
    dest = std::move(source);
    // Stealing must not allocate fresh node storage.
    CHECK(allocation_stats().total_allocations == allocations_before);
    CHECK(dest.size() == 1000);
}

TEST_CASE("POCS: swap exchanges allocators") {
    TrackedMap<int, int> a(CountingAllocator<std::pair<const int, int>>(11));
    TrackedMap<int, int> b(CountingAllocator<std::pair<const int, int>>(22));
    a[1] = 1;
    b[2] = 2;
    using std::swap;
    swap(a, b);
    CHECK(a.get_allocator().id == 22);
    CHECK(b.get_allocator().id == 11);
    CHECK(a.contains(2));
    CHECK(b.contains(1));
}

TEST_CASE("allocator-extended constructors compile and work") {
    CountingAllocator<std::pair<const int, std::string>> alloc(5);
    fum::unordered_map<int, std::string, std::hash<int>, std::equal_to<int>,
                       CountingAllocator<std::pair<const int, std::string>>>
        map(16, alloc);
    map[1] = "hello";
    CHECK(map.get_allocator().id == 5);
    CHECK(map.at(1) == "hello");
}

int main() { return fum_test::run_all(); }
