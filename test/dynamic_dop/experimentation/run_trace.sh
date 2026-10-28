#!/bin/bash
set -u

# always operate from this script's own directory, regardless of the caller's
# cwd — matches setup_vms.sh
cd "$(dirname "${BASH_SOURCE[0]}")"

exec > >(tee "$HOME/run_trace.log") 2>&1

# must match setup_vms.sh — kept in sync by hand since these are static and
# duplicating them keeps this script runnable on its own, repeatedly,
# without depending on any state left behind by setup_vms.sh
TARGET_VM_CID=3
NOISY_VM_CID=4

TARGET_SSH_PORT=2222
NOISY_SSH_PORT=2223

# port the trace-cmd agent listens on in each guest; passed to -A as @cid:port
TRACE_AGENT_PORT=823

STRESS_NG_VERSION=0.19.02
GUEST_STRESS="stress-ng-releases/$STRESS_NG_VERSION"

# trace-cmd's per-cpu temp files land in whatever directory record is run
# from — /tmp on this host is tmpfs (RAM-backed) and fills up fast with a
# multi-guest recording, so use a directory backed by real disk instead.
TRACE_OUTPUT_DIR="/root/traces"
mkdir -p "$TRACE_OUTPUT_DIR"

SSH_OPTS=(-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o BatchMode=yes)
TARGET_SSH_CMD=(ssh -p "$TARGET_SSH_PORT" "${SSH_OPTS[@]}" debian@localhost)
NOISY_SSH_CMD=(ssh -p "$NOISY_SSH_PORT" "${SSH_OPTS[@]}" debian@localhost)

# only stops the things this script starts (stress-ng) — not trace-cmd
# agent/record (started manually, outside this script) and deliberately not
# the qemu vm processes either, same reasoning as test/experimentation/run_trace_1.sh
cleanup() {
    echo "[+] Cleaning up..."
    "${NOISY_SSH_CMD[@]}" "sudo pkill -f stress-ng" 2>/dev/null
}
trap cleanup EXIT INT TERM

# ptp_kvm lets trace-cmd synchronize host/guest clocks when combining traces
# via -A below — without it record fails with "Failed to synchronize
# timestamps with guest" as soon as it attempts the sync handshake
"${TARGET_SSH_CMD[@]}" "sudo modprobe ptp_kvm"
"${NOISY_SSH_CMD[@]}" "sudo modprobe ptp_kvm"

# sched_stat_runtime etc. are gated behind schedstats, which the kernel
# disables by default for overhead reasons — without this the agent fails to
# enable those tracepoints and drops the connection entirely
"${TARGET_SSH_CMD[@]}" "sudo sysctl -w kernel.sched_schedstats=1"
"${NOISY_SSH_CMD[@]}" "sudo sysctl -w kernel.sched_schedstats=1"

# trace-cmd agent (in both vms) and trace-cmd record (on this host) are
# started manually, outside this script — same reasoning as run_trace_1.sh:
# restarting agents from here risks racing a record session already mid-
# handshake in another terminal. this script just waits for confirmation
# before moving on to stress-ng + the benchmark.
echo "[+] Start the trace-cmd agent in both vms (port $TRACE_AGENT_PORT) and trace-cmd record on this host, then come back here:"
echo ""
echo "    trace-cmd record -e sched -v \\"
echo "        -A @$TARGET_VM_CID:$TRACE_AGENT_PORT --name vm1 \\"
echo "        -e printk:console -e sys_enter_sched_setaffinity -e sys_enter_futex -e bpf_trace -e sched -v \\"
echo "        -A @$NOISY_VM_CID:$TRACE_AGENT_PORT --name vm2 \\"
echo "        -e sched -v \\"
echo "        -o $TRACE_OUTPUT_DIR/dop-pvsched-trace.dat"
echo ""
read -r -p "Press Enter once it shows 'Negotiated kvm time sync protocol' for both guests... "

# start noise with the container-built scheduler-workload stress-ng (see
# dev-tools/stress-ng/README) instead of the distro package — Debian 12's
# packaged 0.15.06 lacks the --workload stressor. setup_vms.sh builds,
# publishes and installs it to /usr/local/bin/stress-ng in this vm.
# evaluate.sh, which that README names as the canonical caller of these
# flags, doesn't exist in this repo yet, so the flags below are adapted from
# the Dockerfile's documented smoke-test parameters: --workload-threads is
# scaled to nproc (the smoke test uses 1, just to prove it runs), and
# --timeout/--verbose are dropped since this should run for the whole
# experiment, not a 1-second check — cleanup() pkills it on exit.
# launched via systemd-run into system.slice, not the plain debian ssh login
# user, for the same cgroup pids-controller reason documented in run_trace_1.sh
"${NOISY_SSH_CMD[@]}" "sudo systemd-run --unit=stress-ng --slice=system.slice --collect \
    /usr/local/bin/stress-ng --workload 1 --workload-threads \$(nproc) \
    --workload-dist cluster --workload-method inc64 --workload-load 30 \
    --workload-slice-us 100000 --workload-quanta-us 1000 \
    --seed 1234567890123456789"

# wait for sometime
sleep 10

# run the GOMP_DYNAMIC_POLICY=pvsched benchmark in target vm (blocks until it
# completes). SKIP_INTERNAL_TRACE tells it not to start its own trace-cmd
# record, since the host is already recording both guests via the manually-
# run trace-cmd record above.
"${TARGET_SSH_CMD[@]}" "SKIP_INTERNAL_TRACE=1 bash /mnt/hostshare/dynamic_dop/dop_benchmark.sh"

echo "[+] Benchmark finished — stop trace-cmd record now (Ctrl-C in its terminal)."
