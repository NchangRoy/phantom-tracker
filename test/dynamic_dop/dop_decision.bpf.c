// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * dop_decision BPF test program.
 *
 * Attaches to usdt:libgomp:gomp:dop_decision, fired by
 * pv_gomp_dynamic_max_threads() in libgomp/config/linux/proc.c once
 * Juunansei has resolved the degree of parallelism (DoP) for a parallel
 * region under GOMP_DYNAMIC_POLICY=pvsched.
 *
 * The probe carries pointers to three Juunansei state structs:
 *   arg0 - struct juunansei_decision *decision
 *   arg1 - struct juunansei_status   *status
 *   arg2 - struct juunansei_context  *context
 *
 * This program dereferences arg0 and arg2 to report the resolved DoP
 * alongside the phantom_average / idle_average pair that produced it.
 */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/usdt.bpf.h>

/* Mirrors struct juunansei_decision in libgomp/pvsched.h. */
struct juunansei_decision {
	__u64 host_watermark;
	__u64 timestamp;
	__s32 phantom_average;
	__s32 decision_nr;
	__s32 curr_dop;
	__s32 prev_dop;
	__s32 active_dop;
	__u8  padding[28];
} __attribute__((packed, aligned(64)));

/* Mirrors struct juunansei_context in libgomp/pvsched.h. */
struct juunansei_context {
	__s32 phantom_average;
	__s32 idle_average;
	__s32 min_idle_average;
	__u8  padding[52];
} __attribute__((aligned(64)));

/*
 * Program: usdt:libgomp:gomp:dop_decision
 *
 * Fires once per parallel region after Juunansei has resolved the DoP.
 * Reads decision.curr_dop, decision.phantom_average and
 * context.idle_average through the pointers carried by the probe.
 */
SEC("usdt")
int gomp_dop_decision_handler(struct pt_regs *ctx)
{
	struct juunansei_decision decision = {};
	struct juunansei_context context = {};
	__u64 decision_addr = 0, context_addr = 0;
	__u32 core = bpf_get_smp_processor_id();
	__u32 omp_tid = (__u32)bpf_get_current_pid_tgid();

	if (bpf_usdt_arg(ctx, 0, (long *)&decision_addr) < 0)
		return 0;
	if (bpf_usdt_arg(ctx, 2, (long *)&context_addr) < 0)
		return 0;

	if (bpf_probe_read_user(&decision, sizeof(decision),
				 (void *)decision_addr) < 0)
		return 0;
	if (bpf_probe_read_user(&context, sizeof(context),
				 (void *)context_addr) < 0)
		return 0;

	bpf_printk("CPU %u: OMP thread %u dop=%d phantom_avg=%d idle_avg=%d\n",
		   core, omp_tid, decision.curr_dop,
		   decision.phantom_average, context.idle_average);

	return 0;
}

char _license[] SEC("license") = "GPL";
