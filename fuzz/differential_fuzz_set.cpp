// Differential fuzzer for fum::unordered_set (mirrors fuzz/differential_fuzz.cpp
// for the map).  A pseudo-random operation stream is applied identically to a
// fum::unordered_set and a std::unordered_set oracle, asserting they always
// agree on membership and size, with periodic full cross-checks and an
// open-addressing load-factor invariant check.
//
//   g++ -std=c++20 -fsanitize=address,undefined -Iinclude
//       fuzz/differential_fuzz_set.cpp -o differential_fuzz_set
//   ./differential_fuzz_set [iterations] [seed]
//
// Define FUM_FUZZ_LIBFUZZER to expose an LLVMFuzzerTestOneInput entry point.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <unordered_set>
#include <vector>

#include "fum/unordered_set.hpp"

namespace {

class ByteStream {
  public:
    ByteStream(const std::uint8_t* data, std::size_t size)
        : data_(data), size_(size) {}
    std::uint8_t next_byte() {
        if (cursor_ >= size_) return 0;
        return data_[cursor_++];
    }
    std::uint32_t next_u32() {
        std::uint32_t value = 0;
        for (int i = 0; i < 4; ++i) value = (value << 8) | next_byte();
        return value;
    }
    bool exhausted() const { return cursor_ >= size_; }

  private:
    const std::uint8_t* data_;
    std::size_t size_;
    std::size_t cursor_ = 0;
};

constexpr int kKeyDomain = 256;

[[noreturn]] void fail(const char* message) {
    std::fprintf(stderr, "SET DIFFERENTIAL FAILURE: %s\n", message);
    std::abort();
}
void check(bool condition, const char* message) {
    if (!condition) fail(message);
}

void full_cross_check(const fum::unordered_set<int>& tested,
                      const std::unordered_set<int>& oracle) {
    check(tested.size() == oracle.size(), "size mismatch");
    for (int key : oracle) {
        check(tested.contains(key), "key missing from tested set");
        check(tested.count(key) == 1, "count should be 1 for present key");
        check(tested.find(key) != tested.end(), "find disagrees with contains");
    }
    std::size_t iterated = 0;
    for (int key : tested) {
        check(oracle.count(key) == 1, "tested has a key the oracle lacks");
        ++iterated;
    }
    check(iterated == tested.size(), "iteration count != size");
    check(tested.load_factor() <= tested.max_load_factor() + 1e-6f,
          "load factor exceeds the maximum");
}

bool apply_one_operation(ByteStream& stream, fum::unordered_set<int>& tested,
                         std::unordered_set<int>& oracle) {
    if (stream.exhausted()) return false;
    const std::uint8_t opcode = stream.next_byte() % 12;
    const int key = static_cast<int>(stream.next_u32() % kKeyDomain);

    switch (opcode) {
        case 0:
        case 1:
        case 2: {  // insert
            const bool a = tested.insert(key).second;
            const bool b = oracle.insert(key).second;
            check(a == b, "insert insertedness mismatch");
            break;
        }
        case 3: {  // emplace
            const bool a = tested.emplace(key).second;
            const bool b = oracle.emplace(key).second;
            check(a == b, "emplace insertedness mismatch");
            break;
        }
        case 4:
        case 5: {  // erase by key
            check(tested.erase(key) == oracle.erase(key), "erase count mismatch");
            break;
        }
        case 6: {  // erase by iterator if present
            auto it = tested.find(key);
            if (it != tested.end()) {
                tested.erase(it);
                oracle.erase(key);
            }
            break;
        }
        case 7: {  // membership agreement
            check(tested.contains(key) == (oracle.count(key) != 0),
                  "containment mismatch");
            break;
        }
        case 8: {  // rehash / reserve
            const std::size_t request = stream.next_u32() % 4096;
            tested.rehash(request);
            oracle.rehash(request);
            break;
        }
        case 9: {  // extract + reinsert
            auto node = tested.extract(key);
            if (!node.empty()) {
                check(oracle.count(key) == 1, "extract present mismatch");
                tested.insert(std::move(node));
            } else {
                check(oracle.count(key) == 0, "extract absent mismatch");
            }
            break;
        }
        case 10: {  // copy round-trip
            fum::unordered_set<int> copy = tested;
            check(copy == tested, "copy not equal to original");
            tested = copy;
            break;
        }
        case 11: {  // swap round-trip
            fum::unordered_set<int> other;
            other.insert(key);
            using std::swap;
            swap(tested, other);
            swap(tested, other);
            break;
        }
    }

    check(tested.size() == oracle.size(), "size mismatch after operation");
    check(tested.contains(key) == (oracle.count(key) != 0),
          "containment mismatch after op");
    return true;
}

int run_operations(const std::uint8_t* data, std::size_t size) {
    ByteStream stream(data, size);
    fum::unordered_set<int> tested;
    std::unordered_set<int> oracle;
    long operations = 0;
    while (apply_one_operation(stream, tested, oracle)) {
        if ((++operations & 0x3FF) == 0) full_cross_check(tested, oracle);
    }
    full_cross_check(tested, oracle);
    return 0;
}

}  // namespace

#ifdef FUM_FUZZ_LIBFUZZER
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
    return run_operations(data, size);
}
#else
static std::uint64_t g_state;
static std::uint8_t prng_byte() {
    g_state ^= g_state << 13;
    g_state ^= g_state >> 7;
    g_state ^= g_state << 17;
    return static_cast<std::uint8_t>(g_state >> 24);
}

int main(int argc, char** argv) {
    long iterations = (argc > 1) ? std::strtol(argv[1], nullptr, 10) : 200000;
    g_state =
        (argc > 2) ? std::strtoull(argv[2], nullptr, 10) : 0x9e3779b97f4a7c15ULL;
    if (g_state == 0) g_state = 0x9e3779b97f4a7c15ULL;

    const std::size_t bytes_per_op = 5;  // op byte + key u32
    std::vector<std::uint8_t> buffer(static_cast<std::size_t>(iterations) *
                                     bytes_per_op);
    for (auto& byte : buffer) byte = prng_byte();

    const int result = run_operations(buffer.data(), buffer.size());
    if (result == 0) {
        std::printf("set differential fuzz OK: %ld iterations, seed-driven\n",
                    iterations);
    }
    return result;
}
#endif
