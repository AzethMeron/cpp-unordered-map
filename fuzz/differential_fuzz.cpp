// Differential fuzzer for fum::unordered_map.
//
// A pseudo-random stream of operations is applied identically to a
// fum::unordered_map and a std::unordered_map (the oracle).  After every
// operation we assert the two agree on size and on the value for a sampled key,
// and we periodically perform a full cross-check plus internal-invariant
// checks.  Any divergence aborts with a non-zero exit code.
//
// Two front-ends share the same core:
//   * Standalone (default): `main` derives a byte stream from a seeded PRNG and
//     drives many iterations.  Runs cleanly under ASan/UBSan/Valgrind.
//         g++ -std=c++20 -fsanitize=address,undefined -Iinclude
//             fuzz/differential_fuzz.cpp -o differential_fuzz
//         ./differential_fuzz [iterations] [seed]
//   * libFuzzer (define FUM_FUZZ_LIBFUZZER): exposes LLVMFuzzerTestOneInput so a
//     coverage-guided fuzzer can drive the same op decoder from raw input.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#include "fum/unordered_map.hpp"

namespace {

// A small pull-based stream of bytes that the operation decoder consumes.
// Backed either by fuzzer-provided data or by a PRNG-generated buffer.
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

// Keys are kept in a small range so that collisions, re-insertions and erases of
// present keys all happen frequently.
constexpr int kKeyDomain = 256;

[[noreturn]] void fail(const char* message) {
    std::fprintf(stderr, "DIFFERENTIAL FAILURE: %s\n", message);
    std::abort();
}

void check(bool condition, const char* message) {
    if (!condition) fail(message);
}

// Verify the two maps describe exactly the same mapping, and that the tested
// map's internal invariants hold.
void full_cross_check(const fum::unordered_map<int, int>& tested,
                      const std::unordered_map<int, int>& oracle) {
    check(tested.size() == oracle.size(), "size mismatch");

    // Every oracle entry is present in tested with the same value.
    for (const auto& [key, value] : oracle) {
        auto it = tested.find(key);
        check(it != tested.end(), "key missing from tested map");
        check(it->second == value, "value mismatch (oracle -> tested)");
        check(tested.contains(key), "contains disagrees with find");
        check(tested.count(key) == 1, "count should be 1 for present key");
        check(tested.at(key) == value, "at() value mismatch");
    }

    // Every tested entry is present in the oracle; iteration visits exactly
    // size() elements with no duplicates.
    std::size_t iterated = 0;
    for (const auto& [key, value] : tested) {
        auto it = oracle.find(key);
        check(it != oracle.end(), "tested has a key the oracle lacks");
        check(it->second == value, "value mismatch (tested -> oracle)");
        ++iterated;
    }
    check(iterated == tested.size(), "iteration count != size");

    // Load factor invariant of the open-addressing table.
    check(tested.load_factor() <= tested.max_load_factor() + 1e-6f,
          "load factor exceeds the maximum");
}

// Apply one decoded operation to both maps.  Returns false when the stream is
// exhausted and no further progress can be made.
bool apply_one_operation(ByteStream& stream, fum::unordered_map<int, int>& tested,
                         std::unordered_map<int, int>& oracle) {
    if (stream.exhausted()) return false;

    const std::uint8_t opcode = stream.next_byte() % 16;
    const int key = static_cast<int>(stream.next_u32() % kKeyDomain);
    const int value = static_cast<int>(stream.next_u32());

    switch (opcode) {
        case 0:
        case 1: {  // operator[]
            tested[key] = value;
            oracle[key] = value;
            break;
        }
        case 2: {  // insert (no overwrite)
            auto a = tested.insert({key, value});
            auto b = oracle.insert({key, value});
            check(a.second == b.second, "insert insertedness mismatch");
            break;
        }
        case 3: {  // emplace
            auto a = tested.emplace(key, value);
            auto b = oracle.emplace(key, value);
            check(a.second == b.second, "emplace insertedness mismatch");
            break;
        }
        case 4: {  // try_emplace
            auto a = tested.try_emplace(key, value);
            auto b = oracle.try_emplace(key, value);
            check(a.second == b.second, "try_emplace insertedness mismatch");
            break;
        }
        case 5: {  // insert_or_assign
            auto a = tested.insert_or_assign(key, value);
            auto b = oracle.insert_or_assign(key, value);
            check(a.second == b.second, "insert_or_assign mismatch");
            break;
        }
        case 6:
        case 7: {  // erase by key
            check(tested.erase(key) == oracle.erase(key), "erase count mismatch");
            break;
        }
        case 8: {  // erase by iterator (if present)
            auto it = tested.find(key);
            if (it != tested.end()) {
                tested.erase(it);
                oracle.erase(key);
            }
            break;
        }
        case 9: {  // at / find for a present key
            auto it = tested.find(key);
            const bool present = oracle.count(key) != 0;
            check((it != tested.end()) == present, "find presence mismatch");
            if (present) check(it->second == oracle.at(key), "find value mismatch");
            break;
        }
        case 10: {  // rehash to a (bounded) requested size
            const std::size_t request = stream.next_u32() % 4096;
            tested.rehash(request);
            oracle.rehash(request);
            break;
        }
        case 11: {  // reserve
            const std::size_t request = stream.next_u32() % 4096;
            tested.reserve(request);
            oracle.reserve(request);
            break;
        }
        case 12: {  // clear (occasionally)
            if ((value & 0x3F) == 0) {
                tested.clear();
                oracle.clear();
            }
            break;
        }
        case 13: {  // extract + reinsert via node handle
            auto node = tested.extract(key);
            if (!node.empty()) {
                check(oracle.count(key) == 1, "extract present mismatch");
                tested.insert(std::move(node));  // put it straight back
            } else {
                check(oracle.count(key) == 0, "extract absent mismatch");
            }
            break;
        }
        case 14: {  // copy round-trip
            fum::unordered_map<int, int> copy = tested;
            check(copy == tested, "copy not equal to original");
            tested = copy;  // self-consistent reassignment
            break;
        }
        case 15: {  // swap round-trip with a temporary
            fum::unordered_map<int, int> other;
            other[key] = value;
            using std::swap;
            swap(tested, other);
            swap(tested, other);  // swap back: tested restored
            break;
        }
    }

    // Cheap per-operation agreement check.
    check(tested.size() == oracle.size(), "size mismatch after operation");
    const bool present = oracle.count(key) != 0;
    check(tested.contains(key) == present, "containment mismatch after op");
    return true;
}

int run_operations(const std::uint8_t* data, std::size_t size) {
    ByteStream stream(data, size);
    fum::unordered_map<int, int> tested;
    std::unordered_map<int, int> oracle;

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
// A simple xorshift PRNG so the standalone driver is fully deterministic and
// has no dependence on std::random's implementation details.
static std::uint64_t g_state;
static std::uint8_t prng_byte() {
    g_state ^= g_state << 13;
    g_state ^= g_state >> 7;
    g_state ^= g_state << 17;
    return static_cast<std::uint8_t>(g_state >> 24);
}

int main(int argc, char** argv) {
    long iterations = (argc > 1) ? std::strtol(argv[1], nullptr, 10) : 200000;
    g_state = (argc > 2) ? std::strtoull(argv[2], nullptr, 10) : 0x9e3779b97f4a7c15ULL;
    if (g_state == 0) g_state = 0x9e3779b97f4a7c15ULL;

    // Each operation consumes 9 bytes (op + key + value); generate a buffer big
    // enough for the requested iteration count, then replay it.
    const std::size_t bytes_per_op = 9;
    std::vector<std::uint8_t> buffer(static_cast<std::size_t>(iterations) *
                                     bytes_per_op);
    for (auto& byte : buffer) byte = prng_byte();

    const int result = run_operations(buffer.data(), buffer.size());
    if (result == 0) {
        std::printf("differential fuzz OK: %ld iterations, seed-driven\n",
                    iterations);
    }
    return result;
}
#endif
