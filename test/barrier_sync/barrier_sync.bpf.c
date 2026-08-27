// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * barrier_sync BPF test program.
 *
 * Attaches to four points in the OpenMP / kernel stack to verify that
 * pvsched causes OMP threads to block early (via futex) when the host
 * reports a positive phantom average.
 *
 * Programs:
 *   1. gomp_switch_handler          – tp/sched/sched_switch
 *      Detects when a registered OMP thread is preempted.
 *
 *   2. gomp_futex_enter             – tp/syscalls/sys_enter_futex
 *      Detects a registered OMP thread issuing FUTEX_WAIT
 *
 *   3. gomp_do_wait_handler         – usdt:libgomp:gomp:do_wait
 *      Fires at the entry of do_wait, so we can correlate wait entry with
 *      subsequent futex_wait and preemption events.
 *
 *   4. gomp_phantom_average_handler – usdt:libgomp:gomp:phantom_average
 *      Fires whenever libgomp reads the host-reported phantom average,
 *      reporting the value carried in the probe's first argument.
 */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>
#include <bpf/usdt.bpf.h>
#include "pvsched.h"

#define FUTEX_WAIT 0

/*
 * Map: tid (u32) → cpu index (u32)
 * Populated by the omp_thread_reg loader; keyed on thread-id (lower 32 bits
 * of bpf_get_current_pid_tgid()), not on PID/TGID.
 */
struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 1000000);
	__type(key, __u32); /* thread id */
	__type(value, __u32); /* cpu index */
} omp_threads_map SEC(".maps");

/* -----------------------------------------------------------------------
 * Program 1: sched_switch
 *
 * On every context switch, check whether the outgoing thread (prev_pid)
 * is a registered OMP thread and log it if so.
 * ----------------------------------------------------------------------- */
SEC("tp/sched/sched_switch")
int gomp_switch_handler(struct trace_event_raw_sched_switch *ctx)
{
	__u32 core = bpf_get_smp_processor_id();
	__u32 omp_tid = ctx->prev_pid;

	/* bpf_map_lookup_elem returns a *pointer* to the value, or NULL */
	__u32 *res = bpf_map_lookup_elem(&omp_threads_map, &omp_tid);
	if (res != NULL)
		bpf_printk("CPU %u: OMP thread %u preempted\n", core, omp_tid);

	return 0;
}

/* -----------------------------------------------------------------------
 * Program 2: sys_enter_futex
 *
 * When a registered OMP thread issues FUTEX_WAIT, read the host phantom
 * average and log both the average and the thread id.
 * ----------------------------------------------------------------------- */
SEC("tp/syscalls/sys_enter_futex")
int gomp_futex_enter(struct trace_event_raw_sys_enter *ctx)
{
    __u32 core = bpf_get_smp_processor_id();
    __u32 omp_tid = (__u32)bpf_get_current_pid_tgid();

    __u32 *res = bpf_map_lookup_elem(&omp_threads_map, &omp_tid);

    if (res != NULL && ctx->args[1] == FUTEX_WAIT) {
        bpf_printk("CPU %u: OMP thread %u executed futex wait\n",
                   core, omp_tid);
    }

    return 0;
}

/*
 * Program 3: usdt:libgomp:gomp:do_wait
 *
 * Fires at the entry of do_wait, verifying if the calling thread is a
 * registered OMP thread. If yes, prints the thread id and CPU.
 */
SEC("usdt")
int gomp_do_wait_handler(struct pt_regs *ctx)
{
	__u32 core = bpf_get_smp_processor_id();

	/* tid = lower 32 bits of pid_tgid */
	__u32 omp_tid = (__u32)bpf_get_current_pid_tgid();

	__u32 *res = bpf_map_lookup_elem(&omp_threads_map, &omp_tid);
	if (res != NULL)
		bpf_printk("CPU %u: OMP thread %u calling do_wait\n", core,
			   omp_tid);

	return 0;
}

/*
 * Program 4: usdt:libgomp:gomp:phantom_average
 *
 * Fires whenever libgomp reads the host-reported phantom average.
 * arg0 of the probe carries the phantom average value itself, so no
 * kprobe/kretprobe pair on the ivshmem read path is needed anymore.
 */
SEC("usdt")
int gomp_phantom_average_handler(struct pt_regs *ctx)
{
	__u32 core = bpf_get_smp_processor_id();
	__u32 omp_tid = (__u32)bpf_get_current_pid_tgid();
	long phantom_avg = 0;

	if (bpf_usdt_arg(ctx, 0, &phantom_avg) < 0)
		return 0;

	bpf_printk("CPU %u: OMP thread %u phantom average = %ld\n", core,
		   omp_tid, phantom_avg);

	return 0;
}

char _license[] SEC("license") = "GPL";
