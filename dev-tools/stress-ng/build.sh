#!/bin/bash
# SPDX-License-Identifier: GPL-3.0 with GCC-exception-3.1
#
# Contributors:
#   Human: Himadri Chhaya-Shailesh
#   AI: ChatGPT-6.1
#
# Runs inside the build container with an empty host directory at /build.
set -euo pipefail
jobs=${BUILD_JOBS:-1}
[[ "$jobs" =~ ^[1-9][0-9]*$ ]] || { echo 'error: BUILD_JOBS must be positive' >&2; exit 1; }
cd /build
[[ -z $(find . -mindepth 1 -maxdepth 1 -print -quit) ]] || {
    echo 'error: mount a new, empty build directory at /build' >&2; exit 1
}
cp -a --no-preserve=ownership /usr/src/stress-ng source
cp /usr/src/source-sha256.txt source-sha256.txt
export SOURCE_DATE_EPOCH
SOURCE_DATE_EPOCH=$(stat -c %Y /usr/src/stress-ng/Makefile)
printf '%s\n' "$SOURCE_DATE_EPOCH" > source-date-epoch.txt
dpkg-query -W > build-packages.txt
cd source
build_args=(STATIC=1 PRESERVE_CFLAGS=1
    'CFLAGS=-O2 -g -march=x86-64 -mtune=generic -ffile-prefix-map=/build/source=/usr/src/stress-ng')
printf '%q ' make "-j$jobs" "${build_args[@]}" > ../build-command.txt
printf '\n' >> ../build-command.txt
make -j"$jobs" "${build_args[@]}" 2>&1 | tee ../build.log

# Publish only after linking and checking the exact scheduler options we use.
./stress-ng --version | tee ../version.txt
readelf -lW stress-ng > ../program-headers.txt
readelf -dW stress-ng > ../dynamic.txt
if grep -q INTERP ../program-headers.txt || grep -q NEEDED ../dynamic.txt; then
    echo 'error: expected a static binary without shared-library dependencies' >&2
    exit 1
fi
./stress-ng --workload 1 --workload-threads 1 \
    --workload-dist cluster --workload-method inc64 --workload-load 30 \
    --workload-slice-us 100000 --workload-quanta-us 1000 \
    --seed 1234567890123456789 --timeout 1s --verbose \
    2>&1 | tee ../smoke-test.log
cd /build
mkdir release
cp source/stress-ng source/COPYING release/
cp source-sha256.txt source-date-epoch.txt build-packages.txt build-command.txt \
    version.txt program-headers.txt dynamic.txt smoke-test.log release/
cp source/config.h release/build-config.h
cp source/config release/make-config.txt
(cd release && sha256sum stress-ng > SHA256SUMS)
echo 'Completed release: /build/release (full build log: /build/build.log)'
