#!/bin/bash
set -euo pipefail

# re-exec as root (sudo's default HOME reset, not -E, so ~/phantom-tracker
# below resolves to /root/phantom-tracker rather than the calling user's home)
if [[ $EUID -ne 0 ]]; then
    exec sudo bash "$0" "$@"
fi

exec > >(tee "$HOME/run_dop_decision.log") 2>&1

GUEST_LIBGOMP="$HOME/libgomp-releases/dev-1"

cd ~/phantom-tracker

if ! git remote get-url nchangroy >/dev/null 2>&1; then
    git remote add nchangroy https://github.com/NchangRoy/phantom-tracker.git
fi
git fetch nchangroy

# pulls into whatever branch is already checked out here, assuming the same
# branch name exists on nchangroy's fork
BRANCH="$(git rev-parse --abbrev-ref HEAD)"
git pull nchangroy "$BRANCH"

# ---------------------------------------------------------------------
# Build and attach the dop_decision USDT program
# ---------------------------------------------------------------------

cd ~/phantom-tracker/test/dynamic_dop
make LIBGOMP_PATH="$GUEST_LIBGOMP/libgomp.so.1"

# kill any dop_decision.loader left over from a previous run of this script
# — otherwise its probe stays attached alongside the new one, double-firing
pkill -f dop_decision.loader 2>/dev/null
sleep 1

# blocks forever (sleep loop until SIGINT/SIGTERM) to keep its USDT probe
# attached across the benchmark run below — backgrounded and redirected to
# its own log, same reasoning as omp_thread_reg.loader/barrier_sync.loader
# in test/experimentation/target_vm.sh: otherwise it holds this script's tee
# pipe open and whatever invokes this script never sees EOF
../bin/dop_decision.loader > "$HOME/dop_decision_loader.log" 2>&1 &
DOP_LOADER_PID=$!
sleep 3

if ! kill -0 "$DOP_LOADER_PID" 2>/dev/null; then
    echo "error: dop_decision.loader exited early — check $HOME/dop_decision_loader.log" >&2
    exit 1
fi

# ---------------------------------------------------------------------
# Install and compile the NAS CG benchmark against the custom libgomp
# (same approach as test/experimentation/default.sh / barrier_sync.sh:
# point LD_LIBRARY_PATH at the custom runtime rather than the README's
# -L/-rpath/-l:libgomp.so.1 link flags, since that's the established
# convention elsewhere in this project)
# ---------------------------------------------------------------------

NPB_VERSION="NPB3.4.3"
NPB_ARCHIVE="${NPB_VERSION}.tar.gz"
NPB_URL="https://www.nas.nasa.gov/assets/npb/${NPB_ARCHIVE}"

NPB_ROOT="$HOME/$NPB_VERSION"
NPB_DIR="$NPB_ROOT/NPB3.4-OMP"

export LD_LIBRARY_PATH="$GUEST_LIBGOMP:${LD_LIBRARY_PATH:-}"

cd "$HOME"

if [[ ! -d "$NPB_ROOT" ]]; then
    echo "[+] downloading $NPB_ARCHIVE..."
    if command -v wget >/dev/null 2>&1; then
        wget -O "$NPB_ARCHIVE" "$NPB_URL"
    elif command -v curl >/dev/null 2>&1; then
        curl -L -o "$NPB_ARCHIVE" "$NPB_URL"
    else
        echo "error: wget or curl is required" >&2
        exit 1
    fi
    tar -xzf "$NPB_ARCHIVE"
else
    echo "[+] $NPB_ROOT already exists, skipping download"
fi

cd "$NPB_DIR"

if [[ ! -f config/make.def ]]; then
    cp config/make.def.template config/make.def
fi

BINARY="./bin/cg.B.x"

if [[ -x "$BINARY" ]]; then
    echo "[+] $BINARY already built, skipping"
else
    make clean >/dev/null 2>&1 || true
    make cg CLASS=B
fi

echo "[+] libgomp linked into $BINARY:"
ldd "$BINARY" | grep -E 'libgomp\.so'

echo "[+] dop_decision.loader attached (pid $DOP_LOADER_PID, log: $HOME/dop_decision_loader.log)"
echo "[+] NAS CG.B ready at $NPB_DIR/$BINARY"

