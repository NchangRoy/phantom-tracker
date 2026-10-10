#!/bin/bash
set -u

# always operate from this script's own directory, regardless of the caller's
# cwd — matches setup_vms.sh
cd "$(dirname "${BASH_SOURCE[0]}")"

exec > >(tee "$HOME/run_trace_2.log") 2>&1

# must match setup_vms.sh — kept in sync by hand since these are static and
# duplicating them keeps this script runnable on its own, repeatedly,
# without depending on any state left behind by setup_vms.sh
TARGET_VM_CID=3
NOISY_VM_CID=4

TARGET_SSH_PORT=2222
NOISY_SSH_PORT=2223

# port the trace-cmd agent listens on in each guest; passed to -A as @cid:port
TRACE_AGENT_PORT=823

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
# the qemu vm processes either. this phase is the flaky one; re-running this
# script after a failure should not require tearing down and rebuilding the
# vms (gcc build included) from setup_vms.sh
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

# sched_stat_runtime (requested below) is gated behind schedstats, which the
# kernel disables by default for overhead reasons. without this, the agent
# fails to enable that tracepoint and drops the connection entirely — seen
# as dmesg's "Scheduler tracepoints stat_sleep, stat_iowait, stat_blocked and
# stat_runtime require ..." warning, followed by "Failed to receive trace
# response -107" on the host side once the agent closes the connection
"${TARGET_SSH_CMD[@]}" "sudo sysctl -w kernel.sched_schedstats=1"
"${NOISY_SSH_CMD[@]}" "sudo sysctl -w kernel.sched_schedstats=1"

# trace-cmd agent (in both vms) and trace-cmd record (on this host) are
# started manually, outside this script — restarting the agents from here
# risked killing/racing with an agent already mid-handshake with a record
# session started in another terminal. this script just waits for
# confirmation record is up before moving on to stress-ng + the benchmark.
echo "[+] Start the trace-cmd agent in both vms (port $TRACE_AGENT_PORT) and trace-cmd record on this host, then come back here:"
echo ""
echo "    trace-cmd record -e sched -v \\"
echo "        -A @$TARGET_VM_CID:$TRACE_AGENT_PORT --name vm1 \\"
echo "        -e printk:console -e sys_enter_sched_setaffinity -e sys_enter_futex -e ftrace:bprint -e sched -v \\"
echo "        -A @$NOISY_VM_CID:$TRACE_AGENT_PORT --name vm2 \\"
echo "        -e sched -v \\"
echo "        -o $TRACE_OUTPUT_DIR/pvsched-trace.dat"
echo ""
read -r -p "Press Enter once it shows 'Negotiated kvm time sync protocol' for both guests... "

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

# run the pvsched-policy benchmark in target vm (blocks until it completes).
# SKIP_INTERNAL_TRACE tells it not to start its own trace-cmd record, since
# the host is already recording both guests via the manually-run trace-cmd
# record above.
"${TARGET_SSH_CMD[@]}" "SKIP_INTERNAL_TRACE=1 ./pvsched.sh"

echo "[+] Benchmark finished — stop trace-cmd record now (Ctrl-C in its terminal)."
