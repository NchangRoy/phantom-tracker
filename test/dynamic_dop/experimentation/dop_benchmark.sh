#!/bin/bash
set -u

# ~/libgomp-releases and ~/NPB3.4.3 are built under /root by
# run_dop_decision.sh (which also self-elevates), so this needs the same HOME
if [[ $EUID -ne 0 ]]; then
    exec sudo bash "$0" "$@"
fi

exec > >(tee "$HOME/dop_benchmark.log") 2>&1

# ============================================================
# 0. NAS PARALLEL BENCHMARK
#
# duplicated from test/experimentation/default.sh/pvsched.sh/barrier_sync.sh
# (same NPB version/paths) rather than depending on run_dop_decision.sh
# having built it first, so this script is runnable standalone like those
# siblings — the "already built, skip" check below makes re-running either
# one cheap either way.
# ============================================================

NPB_VERSION="NPB3.4.3"
NPB_ARCHIVE="${NPB_VERSION}.tar.gz"
NPB_URL="https://www.nas.nasa.gov/assets/npb/${NPB_ARCHIVE}"

NPB_ROOT="$HOME/$NPB_VERSION"
NPB_DIR="$NPB_ROOT/NPB3.4-OMP"

GUEST_LIBGOMP="$HOME/libgomp-releases/dev-1"

echo "========================================"
echo " NAS Parallel Benchmarks Setup"
echo "========================================"

cd "$HOME" || exit 1

if [ ! -d "$NPB_ROOT" ]; then
    echo
    echo "NPB $NPB_VERSION not found. Downloading..."

    if command -v wget >/dev/null 2>&1; then
        wget -O "$NPB_ARCHIVE" "$NPB_URL"
    elif command -v curl >/dev/null 2>&1; then
        curl -L -o "$NPB_ARCHIVE" "$NPB_URL"
    else
        echo "ERROR: wget or curl is required." >&2
        exit 1
    fi

    tar -xzf "$NPB_ARCHIVE" || { echo "ERROR: failed to extract $NPB_ARCHIVE" >&2; exit 1; }
else
    echo "NPB $NPB_VERSION already exists: $NPB_ROOT"
fi

if [ ! -d "$NPB_DIR" ]; then
    echo "ERROR: NPB OpenMP directory not found: $NPB_DIR" >&2
    exit 1
fi

cd "$NPB_DIR" || exit 1

# ============================================================
# 1. USE CUSTOM LIBGOMP
#
# unlike default.sh/pvsched.sh, there's no custom GCC toolchain here — only
# a standalone libgomp built by dev-tools/libgomp/build.sh, so the stock
# guest gcc/gfortran compiles NPB and LD_LIBRARY_PATH alone steers the
# dynamic linker to the custom runtime at both build and run time
# ============================================================

export LD_LIBRARY_PATH="$GUEST_LIBGOMP:${LD_LIBRARY_PATH:-}"

echo
echo "libgomp:"
ls -l "$GUEST_LIBGOMP"/libgomp* 2>/dev/null || echo "WARNING: libgomp not found at $GUEST_LIBGOMP"

if [ ! -f "config/make.def" ]; then
    [ -f "config/make.def.template" ] || { echo "ERROR: config/make.def.template not found" >&2; exit 1; }
    cp config/make.def.template config/make.def
fi

# ============================================================
# 2. BUILD NAS CG CLASS B
# ============================================================

BINARY="./bin/cg.B.x"

if [ -x "$BINARY" ]; then
    echo "Binary already exists, skipping build: $BINARY"
else
    make clean >/dev/null 2>&1 || true
    make cg CLASS=B || { echo "ERROR: failed to build cg.B.x" >&2; exit 1; }
    [ -x "$BINARY" ] || { echo "ERROR: $BINARY was not created" >&2; exit 1; }
fi

ldd "$BINARY" | grep -E 'libgomp\.so' || echo "WARNING: libgomp not found in ldd output"

if [ "${BUILD_ONLY:-0}" = "1" ]; then
    echo "BUILD_ONLY=1, stopping after build."
    exit 0
fi

# ============================================================
# 3. BENCHMARK SETTINGS
# ============================================================

RUNS=10
LOG_DIR="benchmark_logs"
mkdir -p "$LOG_DIR"

DOP_LOG="$LOG_DIR/dop_pvsched.log"
DOP_TIMES="$LOG_DIR/dop_pvsched_times.txt"
DOP_TRACE="$LOG_DIR/dop_pvsched.dat"
LDD_LOG="$LOG_DIR/ldd.txt"

echo
echo "========================================"
echo " NAS CG.B.X Benchmark — GOMP_DYNAMIC_POLICY=pvsched"
echo "========================================"
echo "Binary : $BINARY"
echo "Runs   : $RUNS"

ldd "$BINARY" | tee "$LDD_LOG"

run_benchmark()
{
    local log_file="$1"
    local times_file="$2"
    local trace_file="$3"

    export OMP_DYNAMIC=true
    export GOMP_DYNAMIC_POLICY=pvsched

    > "$log_file"
    > "$times_file"
    rm -f "$trace_file"

    SKIP_TRACE="${SKIP_INTERNAL_TRACE:-0}"
    TRACE_PID=""

    if [ "$SKIP_TRACE" = "1" ] || [ "$SKIP_TRACE" = "true" ]; then
        echo "Skipping internal trace-cmd execution (external orchestrator tracing active)."
    else
        trace-cmd record -o "$trace_file" \
            -e printk:console -e sys_enter_sched_setaffinity -e sys_enter_futex -e bpf_trace -e sched -v &
        TRACE_PID=$!
        sleep 2

        if ! kill -0 "$TRACE_PID" 2>/dev/null; then
            echo "ERROR: trace-cmd failed to start!" >&2
            exit 1
        fi
    fi

    for ((i=1; i<=RUNS; i++)); do
        echo
        echo "RUN $i/$RUNS (GOMP_DYNAMIC_POLICY=$GOMP_DYNAMIC_POLICY OMP_DYNAMIC=$OMP_DYNAMIC) start: $(date)" | tee -a "$log_file"

        run_output=$(mktemp)
        OMP_DISPLAY_ENV=true "$BINARY" 2>&1 | tee -a "$log_file" | tee "$run_output"
        status=${PIPESTATUS[0]}

        runtime=$(awk -F'=' '
            /Time in seconds/ { gsub(/[[:space:]]/, "", $2); print $2; exit }
        ' "$run_output")
        rm -f "$run_output"

        if [ -z "$runtime" ]; then
            echo "ERROR: Could not find NAS runtime for run $i" >&2
            [ -n "${TRACE_PID:-}" ] && { kill -INT "$TRACE_PID" 2>/dev/null; wait "$TRACE_PID" 2>/dev/null; }
            exit 1
        fi
        echo "$runtime" >> "$times_file"

        echo "END RUN $i/$RUNS: ${runtime}s, exit $status" | tee -a "$log_file"

        if [ "$status" -ne 0 ]; then
            echo "ERROR: Run $i failed! Check $log_file" >&2
            [ -n "${TRACE_PID:-}" ] && { kill -INT "$TRACE_PID" 2>/dev/null; wait "$TRACE_PID" 2>/dev/null; }
            exit 1
        fi
    done

    if [ "$SKIP_TRACE" != "1" ] && [ "$SKIP_TRACE" != "true" ] && [ -n "$TRACE_PID" ]; then
        kill -INT "$TRACE_PID" 2>/dev/null
        wait "$TRACE_PID" 2>/dev/null
    fi

    read -r average stddev <<< "$(
        awk '
        { sum += $1; sumsq += $1 * $1 }
        END {
            mean = sum / NR
            variance = (sumsq / NR) - (mean * mean)
            if (variance < 0) variance = 0
            printf "%.6f %.6f", mean, sqrt(variance)
        }' "$times_file"
    )"

    echo
    echo "========================================"
    echo "RESULTS — GOMP_DYNAMIC_POLICY=pvsched"
    echo "========================================"
    echo "Runs            : $RUNS"
    echo "Average runtime : $average seconds"
    echo "Std deviation   : $stddev seconds"
    echo "Trace           : $trace_file"
    echo "Log             : $log_file"
}

run_benchmark "$DOP_LOG" "$DOP_TIMES" "$DOP_TRACE"

echo
echo "ARTIFACTS:"
echo "  $LDD_LOG"
echo "  $DOP_LOG"
echo "  $DOP_TIMES"
echo "  $DOP_TRACE"
