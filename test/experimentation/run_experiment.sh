#!/bin/bash
set -u

# always operate from this script's own directory, regardless of the caller's
# cwd, so the vm disk images (created relative to cwd by create_vm.sh) always
# land in the same place and get reused across runs instead of silently
# rebuilt from scratch
cd "$(dirname "${BASH_SOURCE[0]}")"

exec > >(tee "$HOME/run_experiment.log") 2>&1

CREATE_VM_SCRIPT_PATH=../../scripts/create_vm.sh

TARGET_VM_CID=3
NOISY_VM_CID=4

TARGET_SSH_PORT=2222
NOISY_SSH_PORT=2223

# port the trace-cmd agent listens on in each guest; passed to -A as cid:port
TRACE_AGENT_PORT=823

SSH_OPTS=(-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o BatchMode=yes)
TARGET_SSH_CMD=(ssh -p "$TARGET_SSH_PORT" "${SSH_OPTS[@]}" debian@localhost)
NOISY_SSH_CMD=(ssh -p "$NOISY_SSH_PORT" "${SSH_OPTS[@]}" debian@localhost)

TRACE_PID=""

cleanup() {
    echo "[+] Cleaning up..."
    [[ -n "$TRACE_PID" ]] && sudo kill -INT "$TRACE_PID" 2>/dev/null
    "${TARGET_SSH_CMD[@]}" "sudo pkill -f 'trace-cmd agent'" 2>/dev/null
    "${NOISY_SSH_CMD[@]}" "sudo pkill -f 'trace-cmd agent'" 2>/dev/null
    "${NOISY_SSH_CMD[@]}" "sudo pkill -f stress-ng" 2>/dev/null
    pkill -f "guest=testvm" 2>/dev/null
    pkill -f "guest=noise" 2>/dev/null
}
trap cleanup EXIT INT TERM

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

# the vm disk images live in scripts/ (see comment above) which sits on the
# host's root partition — too small to hold them (qcow2 images grow to tens
# of GB). relocate any that are still plain files onto /tmp, which has room,
# and leave a symlink behind so create_vm.sh's path-based reuse logic is
# unaffected. skip anything already a symlink (already relocated). make sure
# no qemu process still has the old file open before moving it out from
# under it, or the running vm keeps writing to the orphaned original.
pkill -f "guest=testvm" 2>/dev/null
pkill -f "guest=noise" 2>/dev/null
for f in testvm.qcow2 testvm-seed.iso noise.qcow2 noise-seed.iso; do
    path="$SCRIPTS_DIR/$f"
    if [[ -f "$path" && ! -L "$path" ]]; then
        echo "relocating $f to /tmp..."
        mv "$path" "/tmp/$f" && ln -s "/tmp/$f" "$path"
    fi
done

# grow the target vm's disk image before booting — create_vm.sh's
# --disk-size only takes effect when creating a fresh overlay, but the image
# is normally reused across runs (see comment above), so resize it explicitly
if [[ -f "$SCRIPTS_DIR/testvm.qcow2" ]]; then
    qemu-img resize "$SCRIPTS_DIR/testvm.qcow2" 40G
fi

# run target vm
(cd "$SCRIPTS_DIR" && bash "$CREATE_VM_SCRIPT" --name="testvm" --ssh-pubkey="/root/.ssh/id_ed25519.pub" --cores=36 --mem=16G --type="target" --add-vsock="true" --vsock-cid="$TARGET_VM_CID" --ssh-port="$TARGET_SSH_PORT") &

# run noisy vm
(cd "$SCRIPTS_DIR" && bash "$CREATE_VM_SCRIPT" --name="noise" --ssh-pubkey="/root/.ssh/id_ed25519.pub" --cores=36 --mem=16G --type="noise" --add-vsock="true" --vsock-cid="$NOISY_VM_CID" --ssh-port="$NOISY_SSH_PORT") &

# wait for vms to completely boot and ssh to become active
sleep 50

# copy the experiment scripts onto the target vm (they only exist on the host).
# -v surfaces the real reason on failure instead of silently leaving stale
# copies on the guest; fail loudly rather than continuing against stale scripts.
if ! scp -v -P "$TARGET_SSH_PORT" "${SSH_OPTS[@]}" target_vm.sh barrier_sync.sh debian@localhost:~/; then
    echo "error: scp failed to sync target_vm.sh/barrier_sync.sh to the target vm" >&2
    exit 1
fi
"${TARGET_SSH_CMD[@]}" "chmod +x ~/target_vm.sh ~/barrier_sync.sh"

# trace-cmd isn't part of the cloud-init package list, install it on both vms
"${TARGET_SSH_CMD[@]}" "sudo apt-get install -y trace-cmd"
"${NOISY_SSH_CMD[@]}" "sudo apt-get install -y trace-cmd"

#initialize target vm environment
"${TARGET_SSH_CMD[@]}" "sudo ./target_vm.sh"

# ptp_kvm lets trace-cmd synchronize host/guest clocks when combining traces
# via -A below — without it record fails with "Failed to synchronize
# timestamps with guest" as soon as it attempts the sync handshake
"${TARGET_SSH_CMD[@]}" "sudo modprobe ptp_kvm"
"${NOISY_SSH_CMD[@]}" "sudo modprobe ptp_kvm"

# sched_stat_runtime (requested below) is gated behind schedstats, which the
# kernel disables by default for overhead reasons. without this, the agent
# fails to enable that tracepoint and drops the connection entirely — seen
# as dmesg's "Scheduler tracepoints stat_sleep, stat_iowait, stat_blocked and
# stat_runtime require ..." warning, followed by "Failed to receive trace
# response -107" on the host side once the agent closes the connection
"${TARGET_SSH_CMD[@]}" "sudo sysctl -w kernel.sched_schedstats=1"
"${NOISY_SSH_CMD[@]}" "sudo sysctl -w kernel.sched_schedstats=1"

# start trace-cmd agent in both vms, daemonized so the ssh call returns.
# port 823 needs root to bind (< 1024).
"${TARGET_SSH_CMD[@]}" "sudo trace-cmd agent -p $TRACE_AGENT_PORT -D"
"${NOISY_SSH_CMD[@]}" "sudo trace-cmd agent -p $TRACE_AGENT_PORT -D"

# run trace-cmd on host, combined recording across host + both guests.
# written under /tmp, not the script's own (root-fs) directory — the trace
# file grows for the whole benchmark run and the root partition is nearly full.
# output goes to its own log (not the shared tee stream) so a failure reason
# is easy to find instead of buried in run_experiment.log
#
# the host/guest clock-sync handshake (needed for the combined -A recording)
# is flaky right after the agents start — it can fail with "Failed to
# synchronize timestamps with guest" even minutes into the guests' uptime,
# and re-running the exact same command shortly after tends to succeed. so
# retry with backoff instead of a single fixed pre-delay.
TRACE_LOG="/tmp/trace-cmd-record.log"
MAX_TRACE_ATTEMPTS=5
WAIT_TIMEOUT=30
WAIT_INTERVAL=2

for attempt in $(seq 1 "$MAX_TRACE_ATTEMPTS"); do
    echo "[+] Starting trace-cmd record (attempt $attempt/$MAX_TRACE_ATTEMPTS)..."
    sudo trace-cmd record -e sched -e sched_stat_runtime -e bpf_trace \
        -A "$TARGET_VM_CID" -e sched -e sched_stat_runtime -e bpf_trace\
         -A "$NOISY_VM_CID" -e sched -e sched_stat_runtime -e bpf_trace \
        -o /tmp/experiment-trace.dat > "$TRACE_LOG" 2>&1 &
    TRACE_PID=$!

    # actively wait for a definitive outcome instead of a fixed short sleep —
    # the sync handshake can still be in progress (neither succeeded nor
    # failed yet) well past 5 seconds, so a one-shot liveness check after a
    # short sleep can wrongly read "still alive" as success right before it
    # fails. poll until we see either both guests negotiate, an explicit
    # failure in the log, or the process dying, up to WAIT_TIMEOUT.
    elapsed=0
    outcome=""
    while [[ "$elapsed" -lt "$WAIT_TIMEOUT" ]]; do
        if ! kill -0 "$TRACE_PID" 2>/dev/null; then
            wait "$TRACE_PID" 2>/dev/null
            outcome="died (exit code $?)"
            break
        fi
        if grep -q "Failed to synchronize timestamps with guest" "$TRACE_LOG"; then
            outcome="sync failure"
            break
        fi
        if grep -q "Negotiated kvm time sync protocol with guest $TARGET_VM_CID" "$TRACE_LOG" \
            && grep -q "Negotiated kvm time sync protocol with guest $NOISY_VM_CID" "$TRACE_LOG"; then
            outcome="success"
            break
        fi
        sleep "$WAIT_INTERVAL"
        elapsed=$((elapsed + WAIT_INTERVAL))
    done

    if [[ "$outcome" == "success" ]]; then
        break
    fi

    if [[ -z "$outcome" ]]; then
        echo "trace-cmd record on attempt $attempt neither succeeded nor failed within ${WAIT_TIMEOUT}s, giving up on it and retrying" >&2
    else
        echo "trace-cmd record failed on attempt $attempt ($outcome), see $TRACE_LOG:" >&2
    fi
    cat "$TRACE_LOG" >&2
    kill "$TRACE_PID" 2>/dev/null

    if [[ "$attempt" -eq "$MAX_TRACE_ATTEMPTS" ]]; then
        echo "error: trace-cmd record failed after $MAX_TRACE_ATTEMPTS attempts" >&2
        exit 1
    fi

    sleep $((attempt * 5))
done

# run stress-ng on NOISY_VM with varying workloads, backgrounded so it
# overlaps the benchmark instead of blocking ahead of it. no --timeout: it
# should run for the whole experiment, not just the first 2 minutes of it —
# cleanup() pkills it on exit once the benchmark finishes. --oom-avoid makes
# it back off before exhausting guest memory instead of getting stressors
# OOM-killed (bigheap grows unbounded otherwise and blew past 16G alone).
# launched via systemd-run into system.slice, not as the plain debian ssh
# login user — running under the user's own session otherwise puts all of
# stress-ng's forked workers inside that session's restrictive TasksMax
# (pids) cgroup limit, which stress-ng's many concurrent workers blow past
# ("cgroup: fork rejected by pids controller in /user.slice/user-1000.slice")
"${NOISY_SSH_CMD[@]}" "sudo apt-get install -y stress-ng"
"${NOISY_SSH_CMD[@]}" "sudo systemd-run --unit=stress-ng --slice=system.slice --collect stress-ng --random \$(nproc) --oom-avoid"

# wait for sometime
sleep 10

# run benchmark in target vm (blocks until it completes).
# SKIP_INTERNAL_TRACE tells it not to start its own trace-cmd record, since
# the host is already recording both guests via the combined -A session above.
# backgrounded so we can poll trace-cmd's pid alongside it below — the earlier
# one-shot liveness check only catches trace-cmd dying in its first 3 seconds;
# the actual sync handshake can fail later than that, and the whole point of
# this run is worthless without a trace, so a later death must abort too.
"${TARGET_SSH_CMD[@]}" "SKIP_INTERNAL_TRACE=1 ./barrier_sync.sh" &
BENCH_PID=$!

while kill -0 "$BENCH_PID" 2>/dev/null; do
    if ! kill -0 "$TRACE_PID" 2>/dev/null; then
        echo "error: trace-cmd record died mid-run, see $TRACE_LOG:" >&2
        cat "$TRACE_LOG" >&2
        kill "$BENCH_PID" 2>/dev/null
        exit 1
    fi
    sleep 5
done
wait "$BENCH_PID"

echo "[+] Benchmark finished, stopping trace-cmd record (PID: $TRACE_PID)"
sudo kill -INT "$TRACE_PID"
wait "$TRACE_PID" 2>/dev/null
