#!/bin/bash
# SPDX-License-Identifier: GPL-3.0 with GCC-exception-3.1
#
# Contributors:
#   Human: Nchang Roy Fru
#   AI: Claude Sonnet 5
#
# Builds libgomp.so.1 from a host GCC source tree inside the Debian 12
# amd64 container defined by dev-tools/libgomp/Dockerfile. See
# dev-tools/libgomp/README for the full workflow (publishing the result
# to guests, linking against it, etc).
#
# Usage: dev-tools/libgomp/build.sh <gcc-source-dir> [build-dir]
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
IMAGE=phantom-libgomp:debian12

if [[ $# -lt 1 ]]; then
    echo "usage: $0 <gcc-source-dir> [build-dir]" >&2
    exit 1
fi

GCC_SOURCE="$(realpath -- "$1")"
if [[ -n "${2:-}" ]]; then
    GCC_BUILD="$2"
    mkdir -p -- "$GCC_BUILD"
else
    GCC_BUILD="$(mktemp -d "${TMPDIR:-/tmp}/libgomp-build.XXXXXX")"
fi
GCC_BUILD="$(realpath -- "$GCC_BUILD")"

# Each build directory is configured from scratch; reusing one across
# source revisions mixes objects built against different sources.
if [[ -n "$(find "$GCC_BUILD" -mindepth 1 -maxdepth 1 -print -quit)" ]]; then
    echo "error: $GCC_BUILD is not empty; use a fresh build directory per source revision" >&2
    exit 1
fi

echo "building container image $IMAGE..."
docker build --platform linux/amd64 -t "$IMAGE" "$SCRIPT_DIR"

GCC_REVISION="$(git -C "$GCC_SOURCE" rev-parse HEAD 2>/dev/null || echo unknown)"
SOURCE_DATE_EPOCH="$(git -C "$GCC_SOURCE" show -s --format=%ct HEAD 2>/dev/null || date +%s)"
printf '%s\n' "$GCC_REVISION" > "$GCC_BUILD/source-revision.txt"
if git -C "$GCC_SOURCE" status --porcelain 2>/dev/null | grep -q .; then
    git -C "$GCC_SOURCE" status --short > "$GCC_BUILD/source-status.txt"
    git -C "$GCC_SOURCE" diff --binary HEAD > "$GCC_BUILD/source-changes.patch" 2>/dev/null || true
    echo "note: $GCC_SOURCE has uncommitted/untracked changes; recorded under $GCC_BUILD" >&2
fi

echo "building libgomp from $GCC_SOURCE (revision $GCC_REVISION) into $GCC_BUILD..."
docker run --rm --platform linux/amd64 \
    --user "$(id -u):$(id -g)" \
    --mount "type=bind,src=$GCC_SOURCE,dst=/src,readonly" \
    --mount "type=bind,src=$GCC_BUILD,dst=/build" \
    --env SOURCE_DATE_EPOCH="$SOURCE_DATE_EPOCH" \
    --env LC_ALL=C --env TZ=UTC --env BUILD_JOBS="$(nproc)" \
    "$IMAGE" bash -c '
        set -euo pipefail
        cd /build
        dpkg-query -W > build-packages.txt
        /src/configure \
            --build=x86_64-pc-linux-gnu \
            --host=x86_64-pc-linux-gnu \
            --target=x86_64-pc-linux-gnu \
            --disable-bootstrap \
            --disable-multilib \
            --disable-nls \
            --disable-libcc1 \
            --enable-languages=c \
            --with-arch=x86-64 --with-tune=generic \
            CFLAGS="-O2 -g -ffile-prefix-map=/src=/usr/src/gcc -ffile-prefix-map=/build=/usr/src/gcc-build" \
            CXXFLAGS="-O2 -g -ffile-prefix-map=/src=/usr/src/gcc -ffile-prefix-map=/build=/usr/src/gcc-build" \
            CFLAGS_FOR_TARGET="-O2 -g -ffile-prefix-map=/src=/usr/src/gcc -ffile-prefix-map=/build=/usr/src/gcc-build" \
            2>&1 | tee configure.log
        make -j"$BUILD_JOBS" all-target-libgomp 2>&1 | tee build.log
        lib=x86_64-pc-linux-gnu/libgomp/.libs/libgomp.so.1
        test -r "$lib"
        readelf -d "$lib" > runtime-dynamic.txt
        readelf --version-info "$lib" > runtime-versions.txt
        readelf -n "$lib" > runtime-notes.txt
        ldd "$lib" > runtime-dependencies.txt
        sha256sum "$(readlink -f "$lib")" > runtime-sha256.txt
    '

LIB="$GCC_BUILD/x86_64-pc-linux-gnu/libgomp/.libs/libgomp.so.1"
echo "built: $LIB"
echo "build artifacts: $GCC_BUILD"
