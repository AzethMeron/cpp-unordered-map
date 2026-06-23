#!/usr/bin/env bash
#
# Build and run the four-way comparison (fum vs std vs boost vs absl) and render
# the graphs.  Boost and Abseil are optional — whichever headers are present get
# included; missing ones are skipped automatically.
#
# Usage: scripts/run_comparison.sh [output_dir]   (default: docs/img)
#
# Requirements for the full four-way run:
#   * Boost >= 1.81 headers (boost/unordered/unordered_flat_map.hpp)
#   * Abseil headers + libs (absl/container/flat_hash_map.h)
#   * Python with matplotlib for the plots (optional; CSV is always produced)

set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT_DIR="${1:-${ROOT}/docs/img}"
DATA_CSV="${ROOT}/benchmarks/data/compare_results.csv"
BIN="$(mktemp -d)/compare_maps"

# Relaxed warnings on purpose: third-party headers do not survive the project's
# strict -Wconversion/-Werror set.
CXXFLAGS="-std=c++20 -O3 -DNDEBUG -Wall -I${ROOT}/include -I${ROOT}/benchmarks"

# Abseil's flat_hash_map needs these libraries when linked against the system
# package; harmless to pass even if the header is absent (the symbols simply go
# unreferenced).
ABSL_LIBS="-labsl_hash -labsl_raw_hash_set -labsl_city -labsl_low_level_hash \
-labsl_throw_delegate -labsl_base -labsl_raw_logging_internal \
-labsl_hashtablez_sampler"

echo "Building compare_maps ..."
# shellcheck disable=SC2086
if ! g++ ${CXXFLAGS} "${ROOT}/benchmarks/compare_maps.cpp" -o "${BIN}" ${ABSL_LIBS}; then
    echo "Link with Abseil failed; retrying without Abseil libraries ..."
    # shellcheck disable=SC2086
    g++ ${CXXFLAGS} "${ROOT}/benchmarks/compare_maps.cpp" -o "${BIN}" || exit 1
fi

echo "Running comparison (this takes a few minutes up to N = 4e6) ..."
"${BIN}" "${DATA_CSV}"

if python3 -c "import matplotlib" 2>/dev/null; then
    echo "Rendering graphs into ${OUT_DIR} ..."
    python3 "${ROOT}/benchmarks/plot_sweep.py" "${DATA_CSV}" "${OUT_DIR}" compare
else
    echo "matplotlib not available; CSV written to ${DATA_CSV} (skipping plots)"
fi
echo "Done."
