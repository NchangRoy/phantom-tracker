// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * GOMP_WAIT_POLICY BPF test program.
 *
 * Attaches to three points in the OpenMP / kernel stack to verify that
 * pvsched causes OMP threads to block early (via futex) when the host
 * reports a positive phantom average.
 *
 * Programs:
 *   1. gomp_switch_handler    – tp/sched/sched_switch
 *      Detects when a registered OMP thread is preempted.
 *
 *   2. gomp_futex_enter       – tp/syscalls/sys_enter_futex
 *      Detects a registered OMP thread issuing FUTEX_WAIT and reads the
 *      current phantom average from the host via ivshmem kfunc.
 *
 *   3. gomp_do_wait_handler   – uprobe on callers of do_wait (libgomp)
 *      Fires at the entry of functions that call do_wait, so we can
 *      correlate wait entry with subsequent futex_wait and preemption events.
 */

#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>
#include "vmlinux.h"
#include "pvsched.h"

#define FUTEX_WAIT 0

/*
 * Kfunc exported by host_ivshmem.ko.
 * Reads one hg_message slot written by the host into *hg_msg.
 * index = H2G_LATEST_SLOT (0) for the most-recent phantom average.
 */
extern int bpf_host_ivshmem_h2g_read(__u32 index,
				      struct hg_message *hg_msg) __ksym;

/*
 * Map: tid (u32) → cpu index (u32)
 * Populated by the omp_thread_reg loader; keyed on thread-id (lower 32 bits
 * of bpf_get_current_pid_tgid()), not on PID/TGID.
 */
struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 1000000);
	__type(key, __u32);  /* thread id */
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
	__u32 core    = bpf_get_smp_processor_id();
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
	struct hg_message msg = {};
	__u32 core = bpf_get_smp_processor_id();

	/* tid = lower 32 bits of pid_tgid (upper 32 bits = tgid/pid) */
	__u32 omp_tid = (__u32)bpf_get_current_pid_tgid();

	__u32 *res = bpf_map_lookup_elem(&omp_threads_map, &omp_tid);
	__u64 ops  = ctx->args[1];

	if (res != NULL && ops == FUTEX_WAIT) {
		/* Read the latest phantom average written by the host */
		bpf_host_ivshmem_h2g_read(H2G_LATEST_SLOT, &msg);

		/* msg.msg is u64, compare to 0 (not NULL) */
		if (msg.msg != 0)
			bpf_printk("CPU %u: Phantom Average is %llu\n",
				   core, msg.msg);

		bpf_printk("CPU %u: OMP thread %u executed futex wait\n",
			   core, omp_tid);
	}

	return 0;
}

/*
 * Params:
         struct pt_regs *ctx: register state at the uprobe site
 * Objective:
         Fires at the entry of functions that call do_wait, verifying
         if the calling thread is a registered OMP thread.
         If yes, prints the thread id and CPU.
 */
SEC("uprobe")
int gomp_do_wait_handler(struct pt_regs *ctx)
{
	__u32 core    = bpf_get_smp_processor_id();

	/* tid = lower 32 bits of pid_tgid */
	__u32 omp_tid = (__u32)bpf_get_current_pid_tgid();

	__u32 *res = bpf_map_lookup_elem(&omp_threads_map, &omp_tid);
	if (res != NULL)
		bpf_printk("CPU %u: OMP thread %u calling do_wait\n",
			   core, omp_tid);

	return 0;
}

char _license[] SEC("license") = "GPL";