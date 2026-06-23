// Instrumented helper types shared across the test suite:
//   * Tracked        - counts live instances to catch leaks / double-frees.
//   * ThrowOnNth     - throws from its constructor to exercise exception safety.
//   * CountingAllocator - a stateful allocator to verify allocator-awareness.
//   * BadHash        - a deliberately terrible hash for adversarial scenarios.

#ifndef FUM_TEST_TYPES_HPP
#define FUM_TEST_TYPES_HPP

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <utility>

namespace fum_test {

// ---------------------------------------------------------------------------
// Tracked: a move/copyable value that tracks the number of live instances so
// tests can assert that every constructed element is eventually destroyed.
// ---------------------------------------------------------------------------
struct LiveCounter {
    long alive = 0;
    long constructed = 0;
    long destroyed = 0;
    void reset() { alive = constructed = destroyed = 0; }
};

inline LiveCounter& live_counter() {
    static LiveCounter counter;
    return counter;
}

struct Tracked {
    int value = 0;

    Tracked() { gain(); }
    Tracked(int v) : value(v) { gain(); }            // NOLINT: implicit on purpose
    Tracked(const Tracked& other) : value(other.value) { gain(); }
    Tracked(Tracked&& other) noexcept : value(other.value) {
        other.value = -1;
        gain();
    }
    Tracked& operator=(const Tracked& other) {
        value = other.value;
        return *this;
    }
    Tracked& operator=(Tracked&& other) noexcept {
        value = other.value;
        other.value = -1;
        return *this;
    }
    ~Tracked() {
        ++live_counter().destroyed;
        --live_counter().alive;
    }

    bool operator==(const Tracked& other) const { return value == other.value; }

  private:
    void gain() {
        ++live_counter().constructed;
        ++live_counter().alive;
    }
};

}  // namespace fum_test

namespace std {
template <>
struct hash<fum_test::Tracked> {
    std::size_t operator()(const fum_test::Tracked& t) const noexcept {
        return std::hash<int>{}(t.value);
    }
};
}  // namespace std

namespace fum_test {

// ---------------------------------------------------------------------------
// ThrowOnNth: throws from the constructor once a global counter reaches a
// configured threshold, to drive exception-safety paths.
// ---------------------------------------------------------------------------
struct ThrowController {
    long countdown = -1;  // -1 disables throwing
    bool should_throw() {
        if (countdown < 0) return false;
        if (countdown == 0) return true;
        --countdown;
        return false;
    }
};
inline ThrowController& throw_controller() {
    static ThrowController controller;
    return controller;
}

struct ThrowOnNth {
    int value = 0;
    ThrowOnNth() { maybe_throw(); }
    ThrowOnNth(int v) : value(v) { maybe_throw(); }  // NOLINT
    ThrowOnNth(const ThrowOnNth& other) : value(other.value) { maybe_throw(); }
    ThrowOnNth(ThrowOnNth&&) = default;
    ThrowOnNth& operator=(const ThrowOnNth&) = default;
    ThrowOnNth& operator=(ThrowOnNth&&) = default;
    bool operator==(const ThrowOnNth& other) const {
        return value == other.value;
    }

  private:
    static void maybe_throw() {
        if (throw_controller().should_throw()) {
            throw std::runtime_error("ThrowOnNth");
        }
    }
};

}  // namespace fum_test

namespace std {
template <>
struct hash<fum_test::ThrowOnNth> {
    std::size_t operator()(const fum_test::ThrowOnNth& t) const noexcept {
        return std::hash<int>{}(t.value);
    }
};
}  // namespace std

namespace fum_test {

// ---------------------------------------------------------------------------
// CountingAllocator: a stateful allocator that tracks outstanding bytes per
// "instance id" so tests can verify (a) no leaks and (b) correct propagation
// semantics.  POCCA/POCMA/POCS are all true so propagation is exercised.
// ---------------------------------------------------------------------------
struct AllocationStats {
    std::size_t outstanding_bytes = 0;
    std::size_t total_allocations = 0;
    std::size_t total_deallocations = 0;
};
inline AllocationStats& allocation_stats() {
    static AllocationStats stats;
    return stats;
}

template <typename T>
struct CountingAllocator {
    using value_type = T;
    using propagate_on_container_copy_assignment = std::true_type;
    using propagate_on_container_move_assignment = std::true_type;
    using propagate_on_container_swap = std::true_type;

    int id = 0;

    CountingAllocator() = default;
    explicit CountingAllocator(int identifier) : id(identifier) {}
    template <typename U>
    CountingAllocator(const CountingAllocator<U>& other) : id(other.id) {}

    T* allocate(std::size_t n) {
        allocation_stats().outstanding_bytes += n * sizeof(T);
        ++allocation_stats().total_allocations;
        return static_cast<T*>(::operator new(n * sizeof(T)));
    }
    void deallocate(T* pointer, std::size_t n) noexcept {
        allocation_stats().outstanding_bytes -= n * sizeof(T);
        ++allocation_stats().total_deallocations;
        ::operator delete(pointer);
    }

    template <typename U>
    bool operator==(const CountingAllocator<U>& other) const {
        return id == other.id;
    }
    template <typename U>
    bool operator!=(const CountingAllocator<U>& other) const {
        return id != other.id;
    }
};

// ---------------------------------------------------------------------------
// BadHash: maps every key onto a tiny set of hash values, forcing maximal
// collisions.  Used to confirm correctness (not performance) under pile-ups.
// ---------------------------------------------------------------------------
struct BadHash {
    std::size_t operator()(int key) const noexcept {
        return static_cast<std::size_t>(key & 1);  // only two distinct hashes
    }
};

// IdentityHash exposes the classic open-addressing attack: with libstdc++
// std::hash<int> being the identity, keys that are multiples of the bucket
// count all share a home slot.  Our mixing step must neutralise this.
struct IdentityHash {
    std::size_t operator()(std::size_t key) const noexcept { return key; }
};

}  // namespace fum_test

#endif  // FUM_TEST_TYPES_HPP
