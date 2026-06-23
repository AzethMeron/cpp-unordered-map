#!/usr/bin/env bash
#
# Build and run every test plus the differential fuzzer under each available
# sanitizer configuration, and (if present) under Valgrind.  Uses direct
# compiler invocations so it works without CMake.
#
# Usage: scripts/run_sanitizers.sh [compiler]   (default: g++)
#
# Exits non-zero if any configuration fails.

set -uo pipefail

CXX="${1:-g++}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$(mktemp -d)"
trap 'rm -rf "${BUILD_DIR}"' EXIT

# The project's mandatory strict flag set (kept in sync with CMakeLists.txt),
# plus -g for readable sanitizer backtraces.
STD_FLAGS="-std=c++20 -O3 -pipe -Wall -Wextra -Wformat=2 -Wconversion \
-Wpointer-arith -Wpedantic -Werror -fstack-protector-all -Wreorder -Wunused \
-Wshadow -g"
INCLUDES="-I${ROOT}/include -I${ROOT}/tests"

TESTS=(test_api test_edge_cases test_allocator test_node_handle test_adversarial
       test_set)
FUZZERS=(differential_fuzz differential_fuzz_set)
FUZZ_ITERS=300000

# Sanitizer configurations to try.  ThreadSanitizer is irrelevant (the container
# is not internally synchronized) so we cover Address + Undefined.
declare -A SANITIZERS=(
    [none]=""
    [asan_ubsan]="-fsanitize=address,undefined -fno-omit-frame-pointer"
)

overall_status=0

run_config() {
    local name="$1"
    local flags="$2"
    echo "=================================================================="
    echo "Configuration: ${name}  (${CXX} ${flags})"
    echo "=================================================================="

    for test_name in "${TESTS[@]}"; do
        local binary="${BUILD_DIR}/${test_name}_${name}"
        # shellcheck disable=SC2086
        if ! ${CXX} ${STD_FLAGS} ${flags} ${INCLUDES} \
                "${ROOT}/tests/${test_name}.cpp" -o "${binary}"; then
            echo "[BUILD FAIL] ${test_name} (${name})"
            overall_status=1
            continue
        fi
        if "${binary}" > "${BUILD_DIR}/${test_name}_${name}.out" 2>&1; then
            echo "[PASS] ${test_name} :: $(tail -n 1 "${BUILD_DIR}/${test_name}_${name}.out")"
        else
            echo "[FAIL] ${test_name} (${name})"
            cat "${BUILD_DIR}/${test_name}_${name}.out"
            overall_status=1
        fi
    done

    # Differential fuzzers (map and set).
    for fuzzer in "${FUZZERS[@]}"; do
        local fuzz_binary="${BUILD_DIR}/${fuzzer}_${name}"
        # shellcheck disable=SC2086
        if ${CXX} ${STD_FLAGS} ${flags} -I"${ROOT}/include" \
                "${ROOT}/fuzz/${fuzzer}.cpp" -o "${fuzz_binary}"; then
            if "${fuzz_binary}" "${FUZZ_ITERS}" \
                    > "${BUILD_DIR}/${fuzzer}_${name}.out" 2>&1; then
                echo "[PASS] ${fuzzer} :: $(tail -n 1 "${BUILD_DIR}/${fuzzer}_${name}.out")"
            else
                echo "[FAIL] ${fuzzer} (${name})"
                cat "${BUILD_DIR}/${fuzzer}_${name}.out"
                overall_status=1
            fi
        else
            echo "[BUILD FAIL] ${fuzzer} (${name})"
            overall_status=1
        fi
    done
}

for config in none asan_ubsan; do
    run_config "${config}" "${SANITIZERS[$config]}"
done

# ---------------------------------------------------------------------------
# Valgrind memcheck on a representative, non-sanitized build (ASan and Valgrind
# must not be combined).
# ---------------------------------------------------------------------------
if command -v valgrind > /dev/null 2>&1; then
    echo "=================================================================="
    echo "Valgrind memcheck"
    echo "=================================================================="
    for vg_test in test_edge_cases test_set; do
        vg_binary="${BUILD_DIR}/${vg_test}_valgrind"
        # shellcheck disable=SC2086
        ${CXX} -std=c++20 -g -O1 ${INCLUDES} \
            "${ROOT}/tests/${vg_test}.cpp" -o "${vg_binary}"
        if valgrind --error-exitcode=1 --leak-check=full \
                --errors-for-leak-kinds=all \
                "${vg_binary}" > "${BUILD_DIR}/valgrind_${vg_test}.out" 2>&1; then
            echo "[PASS] valgrind (${vg_test}): no errors, no leaks"
        else
            echo "[FAIL] valgrind (${vg_test}) reported errors:"
            tail -n 40 "${BUILD_DIR}/valgrind_${vg_test}.out"
            overall_status=1
        fi
    done
else
    echo "valgrind not found; skipping memcheck"
fi

echo "=================================================================="
if [ "${overall_status}" -eq 0 ]; then
    echo "ALL SANITIZER CONFIGURATIONS PASSED"
else
    echo "SOME CONFIGURATIONS FAILED"
fi
exit "${overall_status}"
