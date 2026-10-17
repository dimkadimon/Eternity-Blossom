#!/usr/bin/env bash
# Fetch Blossom VI (GPL-3.0) into external/blossom-vi so that a later configure
# step works without network access:
#
#     ./scripts/fetch_deps.sh
#     cmake -S . -B build -DBLOSSOM_VI_SOURCE_DIR=$PWD/external/blossom-vi
#
# Nothing GPL-licensed is committed to this repository; this script downloads it
# from upstream at your request.
set -eu
DEST=${1:-external/blossom-vi}
if [ -d "$DEST/.git" ]; then
    echo "already present: $DEST"
    exit 0
fi
mkdir -p "$(dirname "$DEST")"
git clone --depth 1 https://github.com/Paul566/blossom-vi.git "$DEST"
echo "Blossom VI cloned into $DEST (GPL-3.0 -- see LICENSE)"

if ! ls /usr/include/boost/version.hpp >/dev/null 2>&1; then
    echo "note: Boost headers not found; the build will use third_party/boost_shim"
fi
