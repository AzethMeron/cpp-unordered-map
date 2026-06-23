// Node handle interface: extract, insert(node), and merge.

#include <string>

#include "fum/unordered_map.hpp"
#include "test_framework.hpp"
#include "test_types.hpp"

using fum_test::live_counter;
using fum_test::Tracked;

TEST_CASE("extract removes element and yields an owning handle") {
    fum::unordered_map<int, std::string> map{{1, "a"}, {2, "b"}, {3, "c"}};
    auto node = map.extract(2);
    CHECK(!node.empty());
    CHECK(static_cast<bool>(node));
    CHECK(node.key() == 2);
    CHECK(node.mapped() == "b");
    CHECK(map.size() == 2);
    CHECK(!map.contains(2));
}

TEST_CASE("extract of a missing key yields an empty handle") {
    fum::unordered_map<int, int> map{{1, 1}};
    auto node = map.extract(99);
    CHECK(node.empty());
    CHECK(!static_cast<bool>(node));
}

TEST_CASE("extract by iterator") {
    fum::unordered_map<int, int> map{{1, 10}, {2, 20}};
    auto it = map.find(1);
    auto node = map.extract(it);
    CHECK(node.key() == 1);
    CHECK(node.mapped() == 10);
    CHECK(map.size() == 1);
}

TEST_CASE("a node handle can be re-inserted, and its key edited first") {
    fum::unordered_map<int, std::string> map{{1, "a"}, {2, "b"}};
    auto node = map.extract(1);
    node.key() = 100;  // mutate the key while detached
    auto result = map.insert(std::move(node));
    CHECK(result.inserted);
    CHECK(result.position->first == 100);
    CHECK(result.position->second == "a");
    CHECK(map.size() == 2);
    CHECK(map.contains(100));
    CHECK(!map.contains(1));
}

TEST_CASE("re-inserting a duplicate key returns the node back to the caller") {
    fum::unordered_map<int, std::string> map{{1, "a"}, {2, "b"}};
    auto node = map.extract(1);
    node.key() = 2;  // now collides with existing key 2
    auto result = map.insert(std::move(node));
    CHECK(!result.inserted);
    CHECK(!result.node.empty());        // handle returned, still owns the value
    CHECK(result.node.key() == 2);
    CHECK(result.position->first == 2);  // points at the existing element
    CHECK(map.size() == 1);              // extract removed 1; re-insert rejected
}

TEST_CASE("extract/insert round trip does not leak") {
    live_counter().reset();
    {
        fum::unordered_map<int, Tracked> map;
        for (int i = 0; i < 100; ++i) map.try_emplace(i, i);
        for (int i = 0; i < 100; ++i) {
            auto node = map.extract(i);
            CHECK(!node.empty());
            map.insert(std::move(node));  // put it straight back
        }
        CHECK(map.size() == 100);
    }
    CHECK(live_counter().alive == 0);
}

TEST_CASE("dropping an extracted handle frees the element") {
    live_counter().reset();
    {
        fum::unordered_map<int, Tracked> map;
        for (int i = 0; i < 10; ++i) map.try_emplace(i, i);
        {
            auto node = map.extract(5);
            CHECK(live_counter().alive == 10);  // still alive, owned by handle
        }                                        // handle destroyed here
        CHECK(live_counter().alive == 9);
        CHECK(map.size() == 9);
    }
    CHECK(live_counter().alive == 0);
}

TEST_CASE("merge moves non-conflicting elements out of the source") {
    fum::unordered_map<int, int> dest{{1, 1}, {2, 2}};
    fum::unordered_map<int, int> source{{2, 99}, {3, 3}, {4, 4}};
    dest.merge(source);
    CHECK(dest.size() == 4);
    CHECK(dest.at(1) == 1);
    CHECK(dest.at(2) == 2);   // pre-existing key 2 kept dest's value
    CHECK(dest.at(3) == 3);
    CHECK(dest.at(4) == 4);
    // Conflicting key remains in the source; merged ones are gone.
    CHECK(source.size() == 1);
    CHECK(source.contains(2));
    CHECK(!source.contains(3));
    CHECK(!source.contains(4));
}

TEST_CASE("merge from an rvalue map") {
    fum::unordered_map<int, int> dest{{1, 1}};
    fum::unordered_map<int, int> source{{2, 2}, {3, 3}};
    dest.merge(std::move(source));
    CHECK(dest.size() == 3);
}

TEST_CASE("merge between maps with different hash/equal types") {
    fum::unordered_map<int, int> dest{{1, 1}};
    fum::unordered_map<int, int, fum_test::BadHash> source{{2, 2}, {3, 3}};
    dest.merge(source);
    CHECK(dest.size() == 3);
    CHECK(dest.at(2) == 2);
    CHECK(source.empty());
}

int main() { return fum_test::run_all(); }
