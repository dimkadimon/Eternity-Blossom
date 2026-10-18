#!/usr/bin/env bash
# Build eternity-blossom with a single g++ invocation -- no CMake required.
#
#   bash scripts/build.sh
#
# Blossom VI (GPL-3.0, github.com/Paul566/blossom-vi) is cloned on first use
# into $BLOSSOM_VI_SOURCE_DIR (default: ~/blossom-vi).  The project does not
# redistribute it; set BLOSSOM_VI_SOURCE_DIR to an existing checkout to avoid
# the clone, or build with --no-blossom for the pure-MIT fallback matcher.
set -euo pipefail
cd "$(dirname "$0")/.."

NO_BLOSSOM=0
for a in "$@"; do [ "$a" = "--no-blossom" ] && NO_BLOSSOM=1; done

BLOSSOM_SRCS=()
BLOSSOM_INC=("-Ithird_party/boost_shim")   # small_vector shim, used if Boost is absent
DEF=()

if [ "$NO_BLOSSOM" -eq 0 ]; then
    BV="${BLOSSOM_VI_SOURCE_DIR:-$HOME/blossom-vi}"
    if [ ! -f "$BV/MWPMSolver.cpp" ]; then
        echo "cloning Blossom VI into $BV ..." >&2
        git clone --depth 1 https://github.com/Paul566/blossom-vi.git "$BV" >&2
    fi
    BLOSSOM_SRCS=("$BV/MWPMSolver.cpp" "$BV/DualUpdater.cpp")
    BLOSSOM_INC=("-I$BV" "-Ithird_party/boost_shim")
    DEF=("-DE2_HAVE_BLOSSOM_VI=1")
else
    echo "building WITHOUT Blossom VI (built-in fallback matcher; MIT only)" >&2
fi

mkdir -p build
g++ -O3 -std=c++20 -Wall \
    -Iinclude "${BLOSSOM_INC[@]}" "${DEF[@]}" \
    src/*.cpp "${BLOSSOM_SRCS[@]}" \
    -o build/eternity-blossom -pthread

echo "built: build/eternity-blossom"
