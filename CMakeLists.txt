cmake_minimum_required(VERSION 3.16)
project(fum_unordered_map CXX)

# ---------------------------------------------------------------------------
# fum is a header-only library exposed as an INTERFACE target.  Consumers just
#   target_link_libraries(my_target PRIVATE fum::fum)
# and #include "fum/unordered_map.hpp".
# ---------------------------------------------------------------------------
add_library(fum INTERFACE)
add_library(fum::fum ALIAS fum)
target_include_directories(fum INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_compile_features(fum INTERFACE cxx_std_20)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(FUM_BUILD_TESTS "Build the test suite" ON)
option(FUM_BUILD_BENCHMARKS "Build the benchmark suite" ON)
option(FUM_BUILD_FUZZERS "Build the differential fuzzer" ON)
option(FUM_WERROR "Treat warnings as errors" ON)

# FUM_SANITIZER: none | address | undefined | address+undefined | thread
set(FUM_SANITIZER "none" CACHE STRING
    "Sanitizer to enable for tests/fuzzers")

# Whether to build the libFuzzer entry point (requires a libFuzzer-capable
# toolchain, e.g. clang with the fuzzer runtime).  Off by default because the
# standalone differential driver already provides coverage everywhere.
option(FUM_FUZZ_LIBFUZZER "Build the libFuzzer entry point" OFF)

# ---------------------------------------------------------------------------
# Shared warning / sanitizer flags applied to our own targets (never to the
# INTERFACE library, so consumers are not forced into our flags).
#
# This is the project's mandatory, strict flag set: every test, fuzzer and
# benchmark compiles cleanly under it (notably -Wconversion and -Wshadow with
# -Werror).
# ---------------------------------------------------------------------------
set(FUM_WARNING_FLAGS
    -O3 -pipe
    -Wall -Wextra -Wpedantic
    -Wformat=2 -Wconversion -Wpointer-arith -Wreorder -Wunused -Wshadow
    -fstack-protector-all)
if(FUM_WERROR)
    list(APPEND FUM_WARNING_FLAGS -Werror)
endif()

set(FUM_SANITIZER_FLAGS "")
if(FUM_SANITIZER STREQUAL "address")
    set(FUM_SANITIZER_FLAGS -fsanitize=address -fno-omit-frame-pointer)
elseif(FUM_SANITIZER STREQUAL "undefined")
    set(FUM_SANITIZER_FLAGS -fsanitize=undefined -fno-omit-frame-pointer)
elseif(FUM_SANITIZER STREQUAL "address+undefined")
    set(FUM_SANITIZER_FLAGS -fsanitize=address,undefined -fno-omit-frame-pointer)
elseif(FUM_SANITIZER STREQUAL "thread")
    set(FUM_SANITIZER_FLAGS -fsanitize=thread -fno-omit-frame-pointer)
endif()

# Helper: apply our standard private flags to a target.
function(fum_configure_target target)
    target_link_libraries(${target} PRIVATE fum::fum)
    target_compile_options(${target} PRIVATE ${FUM_WARNING_FLAGS} ${FUM_SANITIZER_FLAGS})
    target_link_options(${target} PRIVATE ${FUM_SANITIZER_FLAGS})
endfunction()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(FUM_BUILD_TESTS)
    enable_testing()
    set(FUM_TEST_SOURCES
        test_api
        test_edge_cases
        test_allocator
        test_node_handle
        test_adversarial
        test_set)
    foreach(test_name IN LISTS FUM_TEST_SOURCES)
        add_executable(${test_name} tests/${test_name}.cpp)
        fum_configure_target(${test_name})
        target_include_directories(${test_name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
        add_test(NAME ${test_name} COMMAND ${test_name})
    endforeach()

    # The differential fuzzers double as randomized regression tests; run a
    # modest number of iterations as part of `ctest`.
    if(FUM_BUILD_FUZZERS)
        add_executable(differential_fuzz fuzz/differential_fuzz.cpp)
        fum_configure_target(differential_fuzz)
        add_test(NAME differential_fuzz_smoke
                 COMMAND differential_fuzz 100000 12345)

        add_executable(differential_fuzz_set fuzz/differential_fuzz_set.cpp)
        fum_configure_target(differential_fuzz_set)
        add_test(NAME differential_fuzz_set_smoke
                 COMMAND differential_fuzz_set 100000 12345)
    endif()
endif()

# ---------------------------------------------------------------------------
# libFuzzer entry point (optional)
# ---------------------------------------------------------------------------
if(FUM_FUZZ_LIBFUZZER)
    add_executable(differential_libfuzzer fuzz/differential_fuzz.cpp)
    target_link_libraries(differential_libfuzzer PRIVATE fum::fum)
    target_compile_definitions(differential_libfuzzer PRIVATE FUM_FUZZ_LIBFUZZER)
    target_compile_options(differential_libfuzzer PRIVATE
        ${FUM_WARNING_FLAGS} -fsanitize=fuzzer,address,undefined)
    target_link_options(differential_libfuzzer PRIVATE
        -fsanitize=fuzzer,address,undefined)
endif()

# ---------------------------------------------------------------------------
# Benchmarks (always optimised; never built with sanitizers)
# ---------------------------------------------------------------------------
if(FUM_BUILD_BENCHMARKS)
    add_executable(benchmark benchmarks/benchmark.cpp)
    target_link_libraries(benchmark PRIVATE fum::fum)
    # FUM_WARNING_FLAGS already implies -O3; -DNDEBUG drops asserts for timing.
    target_compile_options(benchmark PRIVATE ${FUM_WARNING_FLAGS} -DNDEBUG)

    # Parameter-sweep benchmark; emits CSV consumed by benchmarks/plot_sweep.py.
    add_executable(sweep benchmarks/sweep.cpp)
    target_link_libraries(sweep PRIVATE fum::fum)
    target_include_directories(sweep PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks)
    target_compile_options(sweep PRIVATE ${FUM_WARNING_FLAGS} -DNDEBUG)

    # Optional four-way comparison vs boost::unordered_flat_map and
    # absl::flat_hash_map.  Built only when Abseil's CMake config is found (it
    # supplies the link libraries); Boost headers are picked up automatically.
    # Uses relaxed warnings because third-party headers do not pass the strict
    # flag set.  When unavailable, use scripts/run_comparison.sh instead.
    find_package(absl CONFIG QUIET)
    if(absl_FOUND)
        add_executable(compare_maps benchmarks/compare_maps.cpp)
        target_link_libraries(compare_maps PRIVATE fum::fum absl::flat_hash_map)
        target_include_directories(compare_maps PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks)
        target_compile_options(compare_maps PRIVATE -O3 -DNDEBUG -Wall)
    else()
        message(STATUS
            "fum: Abseil not found; skipping compare_maps "
            "(use scripts/run_comparison.sh)")
    endif()
endif()
