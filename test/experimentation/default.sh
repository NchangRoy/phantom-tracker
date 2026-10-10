#!/bin/bash

set -u

exec > >(tee /home/debian/default.log) 2>&1

# ============================================================
# 0. NAS PARALLEL BENCHMARK
# ============================================================

NPB_VERSION="NPB3.4.3"
NPB_ARCHIVE="${NPB_VERSION}.tar.gz"
NPB_URL="https://www.nas.nasa.gov/assets/npb/${NPB_ARCHIVE}"

NPB_ROOT="/home/debian/NPB3.4.3"
NPB_DIR="$NPB_ROOT/NPB3.4-OMP"

GCC_ROOT="/home/debian/gcc-install"

echo "========================================"
echo " NAS Parallel Benchmarks Setup"
echo "========================================"

# ============================================================
# 1. DOWNLOAD / EXTRACT NPB
# ============================================================

cd "/home/debian" || exit 1

if [ ! -d "$NPB_ROOT" ]; then

    echo
    echo "NPB 3.4.3 not found."
    echo "Downloading..."

    if command -v wget >/dev/null 2>&1; then
        wget -O "$NPB_ARCHIVE" "$NPB_URL"
    elif command -v curl >/dev/null 2>&1; then
        curl -L -o "$NPB_ARCHIVE" "$NPB_URL"
    else
        echo "ERROR: wget or curl is required."
        exit 1
    fi

    if [ $? -ne 0 ]; then
        echo "ERROR: Failed to download NPB."
        exit 1
    fi

    echo
    echo "Extracting $NPB_ARCHIVE..."

    tar -xzf "$NPB_ARCHIVE"

    if [ $? -ne 0 ]; then
        echo "ERROR: Failed to extract NPB."
        exit 1
    fi

else
    echo
    echo "NPB 3.4.3 already exists:"
    echo "  $NPB_ROOT"
fi

# ============================================================
# 2. ENTER NPB OPENMP DIRECTORY
# ============================================================

if [ ! -d "$NPB_DIR" ]; then
    echo "ERROR: NPB OpenMP directory not found:"
    echo "  $NPB_DIR"
    exit 1
fi

cd "$NPB_DIR" || exit 1

echo
echo "NPB directory:"
echo "  $(pwd)"

# ============================================================
# 3. USE CUSTOM GCC / LIBGOMP
# ============================================================

export PATH="$GCC_ROOT/bin:$PATH"
export LD_LIBRARY_PATH="$GCC_ROOT/lib64:${LD_LIBRARY_PATH:-}"

echo
echo "GCC:"
gcc --version | head -n 1

echo
echo "GCC location:"
which gcc

echo
echo "libgomp:"
ls -l "$GCC_ROOT/lib64"/libgomp* 2>/dev/null || \
    echo "WARNING: libgomp not found"

# ============================================================
# 4. CREATE NPB CONFIGURATION
# ============================================================

echo
echo "========================================"
echo " Configuring NPB"
echo "========================================"

if [ ! -f "config/make.def" ]; then

    if [ ! -f "config/make.def.template" ]; then
        echo "ERROR: config/make.def.template not found"
        exit 1
    fi

    echo "Creating config/make.def from template..."

    cp config/make.def.template config/make.def

    if [ $? -ne 0 ]; then
        echo "ERROR: Failed to create config/make.def"
        exit 1
    fi

else
    echo "config/make.def already exists"
fi

echo
echo "NPB compiler configuration:"
grep -E '^(CC|CFLAGS|CLINK|FC|FLINK)' config/make.def || true

# ============================================================
# 5. BUILD NAS CG CLASS B
# ============================================================

echo
echo "========================================"
echo " Building NAS CG Class B"
echo "========================================"

BINARY="./bin/cg.B.x"

if [ -x "$BINARY" ]; then

    echo
    echo "Binary already exists, skipping build:"
    echo "  $BINARY"

else

    make clean >/dev/null 2>&1 || true

    make cg CLASS=B

    if [ $? -ne 0 ]; then
        echo
        echo "ERROR: Failed to build CG.B.x"
        exit 1
    fi

    if [ ! -x "$BINARY" ]; then
        echo
        echo "ERROR: $BINARY was not created"
        exit 1
    fi

    echo
    echo "NAS benchmark successfully built:"
    echo "  $BINARY"

fi

# ============================================================
# 7. CHECK CUSTOM LIBGOMP
# ============================================================

echo
echo "========================================"
echo " Checking libgomp"
echo "========================================"

ldd "$BINARY" | grep -E 'libgomp\.so' || {
    echo "WARNING: libgomp not found in ldd output"
}

# ============================================================
# 8. BENCHMARK SETTINGS
# ============================================================

RUNS=10

LOG_DIR="benchmark_logs"

DEFAULT_LOG="$LOG_DIR/default.log"
DEFAULT_TIMES="$LOG_DIR/default_times.txt"
DEFAULT_TRACE="$LOG_DIR/default.dat"

LDD_LOG="$LOG_DIR/ldd.txt"

mkdir -p "$LOG_DIR"

echo
echo "========================================"
echo " NAS CG.B.X Benchmark"
echo "========================================"
echo "Binary : $BINARY"
echo "Runs   : $RUNS"
echo

# ============================================================
# 9. LDD
# ============================================================

echo "========================================"
echo " LDD OUTPUT"
echo "========================================"

ldd "$BINARY" | tee "$LDD_LOG"

echo
echo "Custom libgomp:"
ldd "$BINARY" | grep -E 'libgomp\.so'

echo
echo "Full ldd output saved to:"
echo "  $LDD_LOG"

# ============================================================
# FUNCTION: RUN BENCHMARK
# ============================================================

run_benchmark()
{
    local policy="$1"
    local log_file="$2"
    local times_file="$3"
    local trace_file="$4"

    echo
    echo "========================================"
    echo "Policy: $policy"
    echo "========================================"

    # --------------------------------------------------------
    # Set GOMP_WAIT_POLICY
    # --------------------------------------------------------

    if [ "$policy" = "default" ]; then
        unset GOMP_WAIT_POLICY
    else
        export GOMP_WAIT_POLICY="$policy"
    fi

    # --------------------------------------------------------
    # Clear previous output
    # --------------------------------------------------------

    > "$log_file"
    > "$times_file"

    rm -f "$trace_file"

    # --------------------------------------------------------
    # START TRACE
    # --------------------------------------------------------

    SKIP_TRACE="${SKIP_INTERNAL_TRACE:-0}"
    TRACE_PID=""

    if [ "$SKIP_TRACE" = "1" ] || [ "$SKIP_TRACE" = "true" ]; then
        echo
        echo "Skipping internal trace-cmd execution (external orchestrator tracing active)."
    else
        echo
        echo "Starting trace-cmd for policy: $policy"
        echo "Trace file: $trace_file"

        trace-cmd record \
            -o "$trace_file" \
            -e printk:console \
            -e sys_enter_sched_setaffinity \
            -e sys_enter_futex \
            -e sched \
            -v \
            &

        TRACE_PID=$!

        sleep 2

        # --------------------------------------------------------
        # Verify trace-cmd
        # --------------------------------------------------------

        if ! kill -0 "$TRACE_PID" 2>/dev/null; then
            echo
            echo "ERROR: trace-cmd failed to start!"
            echo "The benchmark will NOT be executed."
            echo
            echo "Check whether you have permission to access:"
            echo "  /sys/kernel/tracing/tracing_on"
            exit 1
        fi

        echo "trace-cmd PID: $TRACE_PID"
    fi

    # ========================================================
    # RUN BENCHMARK 10 TIMES
    # ========================================================

    for ((i=1; i<=RUNS; i++)); do

        echo
        echo "========================================"
        echo "STARTING RUN $i/$RUNS"
        echo "Policy: ${GOMP_WAIT_POLICY:-<unset>}"
        echo "OMP_DISPLAY_ENV=true"
        echo "========================================"

        {
            echo
            echo "========================================"
            echo "RUN $i/$RUNS"
            echo "Policy: ${GOMP_WAIT_POLICY:-<unset>}"
            echo "OMP_DISPLAY_ENV=true"
            echo "Start: $(date)"
            echo "========================================"
        } | tee -a "$log_file"

        # ----------------------------------------------------
        # Run NAS benchmark
        # ----------------------------------------------------

        run_output=$(mktemp)

        OMP_DISPLAY_ENV=true "$BINARY" 2>&1 | \
            tee -a "$log_file" | \
            tee "$run_output"

        status=${PIPESTATUS[0]}

        # ----------------------------------------------------
        # Extract NAS runtime
        # ----------------------------------------------------

        runtime=$(awk -F'=' '
            /Time in seconds/ {
                gsub(/[[:space:]]/, "", $2);
                print $2;
                exit
            }
        ' "$run_output")

        rm -f "$run_output"

        # ----------------------------------------------------
        # Check runtime
        # ----------------------------------------------------

        if [ -z "$runtime" ]; then

            echo
            echo "ERROR: Could not find NAS runtime for run $i"
            echo "Expected output such as:"
            echo "  Time in seconds = 123.456"

            if [ -n "${TRACE_PID:-}" ]; then
                kill -INT "$TRACE_PID" 2>/dev/null
                wait "$TRACE_PID" 2>/dev/null || true
            fi

            exit 1
        fi

        # ----------------------------------------------------
        # Save runtime
        # ----------------------------------------------------

        echo "$runtime" >> "$times_file"

        # ----------------------------------------------------
        # End-of-run information
        # ----------------------------------------------------

        echo
        echo "========================================"
        echo "END RUN $i/$RUNS"
        echo "NAS Runtime: $runtime seconds"
        echo "Exit status: $status"
        echo "========================================"

        {
            echo
            echo "========================================"
            echo "END RUN $i/$RUNS"
            echo "NAS Runtime: $runtime seconds"
            echo "Exit status: $status"
            echo "End: $(date)"
            echo "========================================"
        } | tee -a "$log_file"

        # ----------------------------------------------------
        # Check benchmark status
        # ----------------------------------------------------

        if [ "$status" -ne 0 ]; then

            echo
            echo "ERROR: Run $i failed!"
            echo "Check: $log_file"

            if [ -n "${TRACE_PID:-}" ]; then
                kill -INT "$TRACE_PID" 2>/dev/null
                wait "$TRACE_PID" 2>/dev/null || true
            fi

            exit 1
        fi

    done

    # ========================================================
    # STOP TRACE
    # ========================================================

    if [ "$SKIP_TRACE" = "1" ] || [ "$SKIP_TRACE" = "true" ]; then
        echo
        echo "========================================"
        echo "TRACE STATUS"
        echo "Policy: $policy"
        echo "External tracing active (skipping local trace stopping)"
        echo "========================================"
    else
        echo
        echo "========================================"
        echo "STOPPING TRACE"
        echo "Policy: $policy"
        echo "========================================"

        if [ -n "$TRACE_PID" ]; then
            kill -INT "$TRACE_PID" 2>/dev/null
            wait "$TRACE_PID" 2>/dev/null || true
        fi

        if [ ! -f "$trace_file" ]; then
            echo "WARNING: Local trace file was not created: $trace_file"
        else
            echo "Trace saved to: $trace_file"
        fi
    fi

    # ========================================================
    # CALCULATE AVERAGE + STANDARD DEVIATION
    # ========================================================

    read -r average stddev <<< "$(
        awk '
        {
            sum += $1
            sumsq += $1 * $1
        }

        END {
            mean = sum / NR
            variance = (sumsq / NR) - (mean * mean)

            if (variance < 0)
                variance = 0

            printf "%.6f %.6f", mean, sqrt(variance)
        }
        ' "$times_file"
    )"

    # ========================================================
    # PRINT POLICY RESULTS
    # ========================================================

    echo
    echo "========================================"
    echo "RESULTS — $policy"
    echo "========================================"
    echo "Runs            : $RUNS"
    echo "Average runtime : $average seconds"
    echo "Std deviation   : $stddev seconds"
    echo "Trace           : $trace_file"
    echo "Log             : $log_file"
    echo "========================================"
}

# ============================================================
# 10. DEFAULT
# ============================================================

run_benchmark \
    "default" \
    "$DEFAULT_LOG" \
    "$DEFAULT_TIMES" \
    "$DEFAULT_TRACE"

# ============================================================
# 11. ARTIFACTS
# ============================================================

echo
echo "========================================"
echo " ARTIFACTS"
echo "========================================"

echo
echo "LD_LIBRARY:"
echo "  $LDD_LOG"

echo
echo "STDIO:"
echo "  $DEFAULT_LOG"

echo
echo "RUNTIMES:"
echo "  $DEFAULT_TIMES"

echo
echo "TRACES:"
echo "  $DEFAULT_TRACE"

echo
echo "========================================"
