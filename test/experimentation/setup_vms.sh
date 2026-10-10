#!/bin/bash
set -u

# always operate from this script's own directory, regardless of the caller's
# cwd, so the vm disk images (created relative to cwd by create_vm.sh) always
# land in the same place and get reused across runs instead of silently
# rebuilt from scratch
cd "$(dirname "${BASH_SOURCE[0]}")"

exec > >(tee "$HOME/setup_vms.log") 2>&1

CREATE_VM_SCRIPT_PATH=../../scripts/create_vm.sh

TARGET_VM_CID=3
NOISY_VM_CID=4

TARGET_SSH_PORT=2222
NOISY_SSH_PORT=2223

SSH_OPTS=(-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o BatchMode=yes)
TARGET_SSH_CMD=(ssh -p "$TARGET_SSH_PORT" "${SSH_OPTS[@]}" debian@localhost)
NOISY_SSH_CMD=(ssh -p "$NOISY_SSH_PORT" "${SSH_OPTS[@]}" debian@localhost)

# no cleanup trap here on purpose — this script only brings the vms up. the
# trace-cmd/benchmark phase (run_trace.sh) is where things have historically
# been flaky, and tearing down + rebuilding these vms (gcc build included)
# on every retry of that phase is expensive. leave the vms running once
# they're up; run_trace.sh is the repeatable unit, not this one.

# create_vm.sh resolves disk images (NAME.qcow2) relative to its own cwd, and
# existing VM images already live in scripts/ from prior manual runs — launch
# it from there (in a subshell, so the rest of this script's cwd is untouched)
# so it finds and reuses those disks instead of building fresh ones.
SCRIPTS_DIR="$(dirname "$CREATE_VM_SCRIPT_PATH")"
CREATE_VM_SCRIPT="$(basename "$CREATE_VM_SCRIPT_PATH")"

# pre-fetch the shared base cloud image once, serially. create_vm.sh defaults
# --base-image to the same file for every VM; if it isn't downloaded yet,
# launching both VMs in parallel races two wgets writing the same file.
(cd "$SCRIPTS_DIR" && . lib/cloudinit.sh && cloudinit_fetch_base_image "debian-12-generic-amd64.qcow2")

# this host's root partition (see df -h: /dev/sda5, hundreds of GB free) has
# plenty of room, so the vm disk images can live directly in scripts/ — no
# need to relocate them anywhere. an older version of this script moved them
# onto /tmp instead (assuming root was too small), leaving a symlink behind
# in scripts/. /tmp is RAM-backed tmpfs, doesn't survive across grid5000's
# per-job node allocations, and fills up fast — and since the repo checkout
# (and any symlink left in it) *can* persist across nodes, that symlink can
# end up dangling on a fresh node. create_vm.sh's qemu-img create then
# follows it and silently recreates the image back on /tmp regardless of
# what we do here, which is how /tmp kept filling up. drop any such symlink
# so a fresh image gets created directly in scripts/ instead. make sure no
# qemu process still has the old file open first, or the running vm keeps
# writing to the orphaned original.
pkill -f "guest=testvm" 2>/dev/null
pkill -f "guest=noise" 2>/dev/null
for f in testvm.qcow2 testvm-seed.iso noise.qcow2 noise-seed.iso; do
    path="$SCRIPTS_DIR/$f"
    if [[ -L "$path" ]]; then
        echo "removing stale /tmp-relocated symlink for $f..."
        rm -f "$path"
    fi
done

# grow the target vm's disk image before booting — create_vm.sh's
# --disk-size only takes effect when creating a fresh overlay, but the image
# is normally reused across runs (see comment above), so resize it explicitly
if [[ -f "$SCRIPTS_DIR/testvm.qcow2" ]]; then
    qemu-img resize "$SCRIPTS_DIR/testvm.qcow2" 40G
fi

# the noisy vm fills its disk over repeated runs — stress-ng's "swap"
# stressor creates on-disk swap files under the guest's root fs, and that
# plus apt caches/trace-cmd agent logs eventually exhausts its default 20G
# image (seen as "No space left on device" from systemd-journald/stress-ng).
# grow it the same way as testvm.qcow2 above.
if [[ -f "$SCRIPTS_DIR/noise.qcow2" ]]; then
    qemu-img resize "$SCRIPTS_DIR/noise.qcow2" 50G
fi

# run target vm
#
# --disk-size only matters the first time create_vm.sh actually creates the
# overlay (cloudinit_create_overlay is a no-op if $DISK_IMG already exists)
# — but on this host (Grid5000: a fresh node per job, so /tmp is always
# empty) the overlay never pre-exists, so this is the size that actually
# sticks; the qemu-img resize calls above only help on hosts where the vm
# disks genuinely persist across runs.
(cd "$SCRIPTS_DIR" && bash "$CREATE_VM_SCRIPT" --name="testvm" --ssh-pubkey="/root/.ssh/id_ed25519.pub" --cores=36 --mem=16G --disk-size=40G --type="target" --add-vsock="true" --vsock-cid="$TARGET_VM_CID" --ssh-port="$TARGET_SSH_PORT") &

# run noisy vm
(cd "$SCRIPTS_DIR" && bash "$CREATE_VM_SCRIPT" --name="noise" --ssh-pubkey="/root/.ssh/id_ed25519.pub" --cores=36 --mem=16G --disk-size=50G --type="noise" --add-vsock="true" --vsock-cid="$NOISY_VM_CID" --ssh-port="$NOISY_SSH_PORT") &

# wait for vms to completely boot and ssh to become active
sleep 50

# qemu-img resize above only grows the virtual disk — the guest's partition
# and filesystem don't actually expand until something inside the guest
# grows them. cloud-init's growpart/resizefs modules are supposed to do this
# on every boot, but that silently didn't take effect for the noisy vm
# before, so grow both explicitly here instead of trusting it implicitly.
# growpart exits 1 with "NOCHANGE" if the partition is already max size,
# which is fine (no set -e here).
"${TARGET_SSH_CMD[@]}" "sudo growpart /dev/vda 1; sudo resize2fs /dev/vda1"
"${NOISY_SSH_CMD[@]}" "sudo growpart /dev/vda 1; sudo resize2fs /dev/vda1"

# copy the experiment scripts onto the target vm (they only exist on the host).
# -v surfaces the real reason on failure instead of silently leaving stale
# copies on the guest; fail loudly rather than continuing against stale scripts.
if ! scp -v -P "$TARGET_SSH_PORT" "${SSH_OPTS[@]}" target_vm.sh barrier_sync.sh default.sh pvsched.sh debian@localhost:~/; then
    echo "error: scp failed to sync target_vm.sh/barrier_sync.sh/default.sh/pvsched.sh to the target vm" >&2
    exit 1
fi
"${TARGET_SSH_CMD[@]}" "chmod +x ~/target_vm.sh ~/barrier_sync.sh ~/default.sh ~/pvsched.sh"

# trace-cmd isn't part of the cloud-init package list, install it on both vms
"${TARGET_SSH_CMD[@]}" "sudo apt-get install -y trace-cmd"
"${NOISY_SSH_CMD[@]}" "sudo apt-get install -y trace-cmd"

#initialize target vm environment
"${TARGET_SSH_CMD[@]}" "sudo ./target_vm.sh"

echo "[+] Setup complete. VMs are up and left running — run ./run_trace.sh next."
