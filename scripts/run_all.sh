#!/usr/bin/env bash
#
# One-stop CMake-based build & test driver.  Configures and runs the suite in
# two flavours via CTest:
#   1. a plain optimised build, and
#   2. an Address+UndefinedBehavior sanitized build,
# then builds the benchmark.  This mirrors what CI would do, but runs locally
# (the project intentionally ships no GitHub Actions workflows).
#
# Usage: scripts/run_all.sh

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${ROOT}"

build_and_test() {
    local label="$1"
    local sanitizer="$2"
    local build_dir="build/${label}"

    echo "=================================================================="
    echo "Build '${label}' (FUM_SANITIZER=${sanitizer})"
    echo "=================================================================="
    cmake -S . -B "${build_dir}" \
        -DCMAKE_BUILD_TYPE=RelWithDebInfo \
        -DFUM_SANITIZER="${sanitizer}" \
        -DFUM_BUILD_BENCHMARKS=ON > /dev/null
    cmake --build "${build_dir}" -j"$(nproc)"
    ( cd "${build_dir}" && ctest --output-on-failure )
}

build_and_test "plain" "none"
build_and_test "asan_ubsan" "address+undefined"

echo "=================================================================="
echo "Running the benchmark (quick scale)"
echo "=================================================================="
./build/plain/benchmark 0 || true

echo "=================================================================="
echo "ALL BUILDS AND TESTS COMPLETED SUCCESSFULLY"
echo "=================================================================="
