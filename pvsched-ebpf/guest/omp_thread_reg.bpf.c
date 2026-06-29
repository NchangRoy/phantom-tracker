// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Guest-side eBPF uprobe: captures OpenMP threads hooking into GOMP_parallel.
 * Loader attaches this to libgomp at runtime using the symbol name,
 * so it works regardless of the libgomp install path.
 */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

char LICENSE[] SEC("license") = "GPL";

/*
 * Map: tid (u32) → cpu index (u32)
 * Records which physical CPU each OpenMP thread was running on
 * when it entered a parallel region.
 */
struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 1000000);
	__type(key, __u32); /* thread id */
	__type(value, __u32); /* cpu index */
} omp_threads_map SEC(".maps");

/*
 * Uprobe on GOMP_parallel (libgomp).
 * Fires when an OpenMP parallel region starts on this thread.
 * Loader attaches this to the correct libgomp path at runtime.
 */
SEC("uprobe")
int capture_omp_master_thread(struct pt_regs *ctx)
{
	/* tid = lower 32 bits of pid_tgid */
	__u32 tid = (__u32)bpf_get_current_pid_tgid();
	__u32 cpu = bpf_get_smp_processor_id();

	bpf_printk("omp master tid=%u cpu=%u\n", tid, cpu);

	bpf_map_update_elem(&omp_threads_map, &tid, &cpu, BPF_ANY);

	return 0;
}

/*
 * Uprobe on gomp_thread_start (libgomp).
 * Fires when an OpenMP  worker thread starts.
 */
SEC("uprobe")
int capture_omp_worker_threads(struct pt_regs *ctx)
{
	__u32 tid = (__u32)bpf_get_current_pid_tgid();
	__u32 cpu = bpf_get_smp_processor_id();

	bpf_printk("omp worker tid=%u cpu=%u\n", tid, cpu);

	bpf_map_update_elem(&omp_threads_map, &tid, &cpu, BPF_ANY);

	return 0;
}
SEC("tp/sched/sched_switch")
int handle_switch(struct trace_event_raw_sched_switch *ctx)
{
	__u32 prev_tid = ctx->prev_pid;
	__u32 next_tid = ctx->next_pid;
	__u32 current_cpu = bpf_get_smp_processor_id();

	if (bpf_map_lookup_elem(&omp_threads_map, &prev_tid)) {
		//update ivshmem slot for this cpu set it to 0
		bpf_printk("updating ivshmem entry for cpu %d to 0\n",
			   current_cpu);
	}

	if (bpf_map_lookup_elem(&omp_threads_map, &next_tid)) {
		//update ivshmem slot for this cpu set it to 1
		bpf_printk("updating ivshmem entry for cpu %d to 1\n",
			   current_cpu);
	}

	return 0;
}