#!/bin/bash
set -euo pipefail

# always operate from this script's own directory, regardless of the
# caller's cwd (see test/experimentation/setup_vms.sh for the original
# pattern this is adapted from)
cd "$(dirname "${BASH_SOURCE[0]}")"

exec > >(tee "$HOME/setup_vms_dop.log") 2>&1

REPO_ROOT="$(cd ../../.. && pwd)"
SCRIPTS_DIR="$REPO_ROOT/scripts"

TARGET_VM_CID=3
NOISY_VM_CID=4
TARGET_SSH_PORT=2222
NOISY_SSH_PORT=2223
SSH_OPTS=(-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o BatchMode=yes)
TARGET_SSH_CMD=(ssh -p "$TARGET_SSH_PORT" "${SSH_OPTS[@]}" debian@localhost)
NOISY_SSH_CMD=(ssh -p "$NOISY_SSH_PORT" "${SSH_OPTS[@]}" debian@localhost)

GCC_SOURCE=/tmp/gcc
GCC_BRANCH=phantom-libgomp
GCC_BUILD=/tmp/src/build/gcc-libgomp-dev
LIBGOMP_RELEASE="$SCRIPTS_DIR/shared/libgomp/dev-1"
GUEST_LIBGOMP=libgomp-releases/dev-1
SHARED_DOP_DIR="$SCRIPTS_DIR/shared/dynamic_dop"

STRESS_NG_VERSION=0.19.02
STRESS_IMAGE="phantom-stress-ng:$STRESS_NG_VERSION-debian12"
STRESS_BUILD="/tmp/src/build/stress-ng-$STRESS_NG_VERSION"
SHARED_STRESS_DIR="$SCRIPTS_DIR/shared/stress-ng/$STRESS_NG_VERSION"
GUEST_STRESS="stress-ng-releases/$STRESS_NG_VERSION"

# ---------------------------------------------------------------------
# Part A — build libgomp with your changes (host)
# ---------------------------------------------------------------------

if ! command -v docker >/dev/null 2>&1; then
    echo "[+] installing docker.io..."
    apt-get update
    apt-get install -y docker.io
fi

if [[ -d "$GCC_SOURCE/.git" ]]; then
    echo "[+] updating existing $GCC_SOURCE to latest $GCC_BRANCH..."
    git -C "$GCC_SOURCE" fetch origin "$GCC_BRANCH"
    git -C "$GCC_SOURCE" checkout "$GCC_BRANCH"
    git -C "$GCC_SOURCE" reset --hard "origin/$GCC_BRANCH"
else
    echo "[+] cloning gcc into $GCC_SOURCE..."
    git clone https://github.com/NchangRoy/gcc.git "$GCC_SOURCE"
    git -C "$GCC_SOURCE" checkout "$GCC_BRANCH"
fi

# build.sh requires a fresh, empty build directory every run, and this is a
# dev loop rebuilding against the latest commit on $GCC_BRANCH each time
# (not the provenance-tracked baseline flow in dev-tools/libgomp/README) —
# wipe any previous build instead of erroring on a non-empty directory
rm -rf "$GCC_BUILD"
mkdir -p "$GCC_BUILD"

"$REPO_ROOT/dev-tools/libgomp/build.sh" "$GCC_SOURCE" "$GCC_BUILD"

# build the scheduler-workload-capable stress-ng binary for the noise vm
# (see dev-tools/stress-ng/README — Debian 12's packaged 0.15.06 lacks the
# --workload stressor run_trace.sh needs). no build.sh wrapper exists for
# this one, so the docker build/run steps from that README are inlined here.
echo "[+] building $STRESS_IMAGE..."
docker build --platform linux/amd64 -t "$STRESS_IMAGE" "$REPO_ROOT/dev-tools/stress-ng"

rm -rf "$STRESS_BUILD"
mkdir -p "$STRESS_BUILD"
docker run --rm --platform linux/amd64 \
    --user "$(id -u):$(id -g)" \
    --mount "type=bind,src=$STRESS_BUILD,dst=/build" \
    --env BUILD_JOBS="$(nproc)" \
    "$STRESS_IMAGE"

# ---------------------------------------------------------------------
# Part B — publish it into the share directory
# ---------------------------------------------------------------------

echo "[+] publishing libgomp to $LIBGOMP_RELEASE..."
mkdir -p "$LIBGOMP_RELEASE"
cp -a "$GCC_BUILD"/x86_64-pc-linux-gnu/libgomp/.libs/libgomp.so* "$LIBGOMP_RELEASE/"
ls -la "$LIBGOMP_RELEASE"

echo "[+] publishing run_dop_decision.sh and dop_benchmark.sh to $SHARED_DOP_DIR..."
mkdir -p "$SHARED_DOP_DIR"
cp -a run_dop_decision.sh dop_benchmark.sh "$SHARED_DOP_DIR/"

echo "[+] publishing stress-ng to $SHARED_STRESS_DIR..."
mkdir -p "$SHARED_STRESS_DIR"
cp -a "$STRESS_BUILD"/release/. "$SHARED_STRESS_DIR/"

# ---------------------------------------------------------------------
# Part C — launch the target VM (with the 9p share) and a noisy VM
# ---------------------------------------------------------------------

# create_vm.sh defaults --base-image to the same file for every VM; fetch it
# once serially first so two parallel launches below don't race two wgets
# writing the same file (see test/experimentation/setup_vms.sh)
(cd "$SCRIPTS_DIR" && . lib/cloudinit.sh && cloudinit_fetch_base_image "debian-12-generic-amd64.qcow2")

pkill -f "guest=testvm" 2>/dev/null || true
pkill -f "guest=noise" 2>/dev/null || true

(cd "$SCRIPTS_DIR" && bash create_vm.sh --name="testvm" --type="target" \
    --ssh-pubkey="/root/.ssh/id_ed25519.pub" --cores=36 --mem=16G \
    --mode=dev --shared-dir=./shared \
    --add-vsock="true" --vsock-cid="$TARGET_VM_CID" --ssh-port="$TARGET_SSH_PORT") &

(cd "$SCRIPTS_DIR" && bash create_vm.sh --name="noise" --type="noise" \
    --ssh-pubkey="/root/.ssh/id_ed25519.pub" --cores=36 --mem=16G \
    --mode=dev --shared-dir=./shared \
    --add-vsock="true" --vsock-cid="$NOISY_VM_CID" --ssh-port="$NOISY_SSH_PORT") &

wait

# give both vms time to finish booting and bring ssh up before connecting
sleep 50

# ---------------------------------------------------------------------
# Part D — inside the guest, copy libgomp locally
# ---------------------------------------------------------------------

"${TARGET_SSH_CMD[@]}" "mkdir -p $GUEST_LIBGOMP"
"${TARGET_SSH_CMD[@]}" "cp -a --no-preserve=ownership /mnt/hostshare/libgomp/dev-1/. $GUEST_LIBGOMP/"
"${TARGET_SSH_CMD[@]}" "readelf -n $GUEST_LIBGOMP/libgomp.so.1"

# ---------------------------------------------------------------------
# Part E — inside the noisy guest, install the custom stress-ng
# ---------------------------------------------------------------------

"${NOISY_SSH_CMD[@]}" "mkdir -p $GUEST_STRESS"
"${NOISY_SSH_CMD[@]}" "cp -a --no-preserve=ownership /mnt/hostshare/stress-ng/$STRESS_NG_VERSION/. $GUEST_STRESS/"
"${NOISY_SSH_CMD[@]}" "cd $GUEST_STRESS && sha256sum -c SHA256SUMS"
"${NOISY_SSH_CMD[@]}" "sudo install -m 0755 $GUEST_STRESS/stress-ng /usr/local/bin/stress-ng"
"${NOISY_SSH_CMD[@]}" "stress-ng --version"

echo "[+] Setup complete. libgomp published at ~/$GUEST_LIBGOMP on testvm."
echo "[+] stress-ng $STRESS_NG_VERSION installed at /usr/local/bin/stress-ng on noise."
echo "[+] On testvm, run: bash /mnt/hostshare/dynamic_dop/run_dop_decision.sh"
echo "[+] testvm: ssh -p $TARGET_SSH_PORT debian@localhost (vsock cid $TARGET_VM_CID)"
echo "[+] noise:  ssh -p $NOISY_SSH_PORT debian@localhost (vsock cid $NOISY_VM_CID)"
echo "[+] Both are up — run ./run_trace.sh next."
