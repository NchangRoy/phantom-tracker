#include"vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>
#include "phantom_tracker.h"
#include "register_vm_bpf.h"
#include "pvsched.h"

char LICENSE[] SEC("license") = "GPL";

#define CLOCK_BOOTTIME 7
#define TIMER_PERIOD_NS 1000000000ULL /* 1s */

/* __percpu isn't defined by this toolchain's bpf_helpers.h; percpu
 * resolution happens via kallsyms + BTF name matching regardless. */
#ifndef __percpu
#define __percpu
#endif
extern struct rq runqueues __ksym __percpu;

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 3000);
	__type(key, __u32);
	__type(value, struct global_runnx_sample);
} collectx_buff_1 SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 3000);
	__type(key, __u32);
	__type(value, struct global_runnx_sample);
} collectx_buff_2 SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, struct global_runnx_tracker_state);
} global_tracker_state SEC(".maps");

struct elem {
	struct bpf_timer timer;
	__u64 started;
};

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, struct elem);
} timer_map SEC(".maps");

struct nr_running_avg_ctx {
	__s64 sum;
	__u64 total_time;
	int nb_samples;
};

static long nr_running_avg_sample_cb(struct bpf_map *map, const void *key, void *value,
				      void *ctx)
{
	struct nr_running_avg_ctx *data = (struct nr_running_avg_ctx *)ctx;
	__u32 k = *(__u32 *)key;

	if (k >= data->nb_samples) {
		return 0;
	}

	struct global_runnx_sample *current_sample = (struct global_runnx_sample *)value;
	__u32 next_k = k + 1;
	struct global_runnx_sample *next_sample = bpf_map_lookup_elem(map, &next_k);
	if (!next_sample) {
		return 0;
	}

	/* Drop out-of-order: cross-CPU races can assign a later index an
	 * earlier timestamp, causing unsigned wrap on subtraction. */
	if (next_sample->timestamp < current_sample->timestamp) {
		return 0;
	}

	__s64 count = current_sample->count;
	if (count < 0) {
		count = 0;
	}

	__u64 duration = next_sample->timestamp - current_sample->timestamp;

	data->sum += ((__s64)count * (__s64)duration);
	data->total_time += duration;

	return 0;
}

/* The per-CPU sum below is #include'd from bin/rq_compare_cpu_loop.h
 * (Makefile-generated, manually duplicated blocks instead of a real `for`
 * loop): this host's bpf-gcc miscompiles a loop around bpf_per_cpu_ptr()
 * as a false "indirect call" once the surrounding function is complex
 * enough. See the Makefile for details. */
SEC("tp_btf/sched_switch")
int rq_compare_switch_handler(__u64 *ctx)
{
	__u32 key = 0;
	struct global_runnx_tracker_state *state = bpf_map_lookup_elem(&global_tracker_state, &key);
	struct global_runnx_sample sample_rec = {};
	__s64 sample = 0;
	__u32 idx;

	if (!state)
		return 0;

#include "rq_compare_cpu_loop.h"

	sample_rec.count = sample;
	sample_rec.timestamp = bpf_ktime_get_ns();

	if (state->is_collectx_in_buff_1 == 1) {
		idx = __sync_fetch_and_add(&state->collection_buff_1_index, 1);
		bpf_map_update_elem(&collectx_buff_1, &idx, &sample_rec, BPF_ANY);
	} else {
		idx = __sync_fetch_and_add(&state->collection_buff_2_index, 1);
		bpf_map_update_elem(&collectx_buff_2, &idx, &sample_rec, BPF_ANY);
	}

	return 0;
}

static int idle_avg_timer_cb(void *map, __u32 *key, struct elem *val)
{
	__u32 state_key = 0;
	struct global_runnx_tracker_state *state =
		bpf_map_lookup_elem(&global_tracker_state, &state_key);

	if (state) {
		struct global_runnx_sample boundary = {};
		__u32 zero = 0;
		__u32 end_idx;
		struct nr_running_avg_ctx avg_ctx = {};
		void *map_to_process;
		__s64 sample = 0;

#include "rq_compare_cpu_loop.h"

		boundary.timestamp = bpf_ktime_get_ns();
		boundary.count = sample;

		if (state->is_collectx_in_buff_1 == 1) {
			bpf_map_update_elem(&collectx_buff_2, &zero, &boundary, BPF_ANY);
			state->collection_buff_2_index = 1;

			__sync_val_compare_and_swap(&state->is_collectx_in_buff_1, 1, 0);

			end_idx = __sync_fetch_and_add(&state->collection_buff_1_index, 1);
			bpf_map_update_elem(&collectx_buff_1, &end_idx, &boundary, BPF_ANY);

			avg_ctx.nb_samples = end_idx;
			map_to_process = &collectx_buff_1;
		} else {
			bpf_map_update_elem(&collectx_buff_1, &zero, &boundary, BPF_ANY);
			state->collection_buff_1_index = 1;

			__sync_val_compare_and_swap(&state->is_collectx_in_buff_1, 0, 1);

			end_idx = __sync_fetch_and_add(&state->collection_buff_2_index, 1);
			bpf_map_update_elem(&collectx_buff_2, &end_idx, &boundary, BPF_ANY);

			avg_ctx.nb_samples = end_idx;
			map_to_process = &collectx_buff_2;
		}

		bpf_for_each_map_elem(map_to_process, nr_running_avg_sample_cb, &avg_ctx, 0);

		state->published_weighted_sum_ns = avg_ctx.sum;
		state->published_window_duration_ns = avg_ctx.total_time;
	}

	bpf_timer_start(&val->timer, TIMER_PERIOD_NS, 0);
	return 0;
}

SEC("tp_btf/sched_switch")
int rq_compare_timer_init(__u64 *ctx)
{
	__u32 key = 0;
	struct elem *e = bpf_map_lookup_elem(&timer_map, &key);
	if (!e)
		return 0;

	if (__sync_lock_test_and_set(&e->started, 1) != 0)
		return 0;

	int ret = bpf_timer_init(&e->timer, &timer_map, CLOCK_BOOTTIME);
	if (ret) {
		bpf_printk("timer_init failed: %d\n", ret);
		__sync_val_compare_and_swap(&e->started, 1, 0);
		return 0;
	}
	bpf_timer_set_callback(&e->timer, idle_avg_timer_cb);
	bpf_timer_start(&e->timer, TIMER_PERIOD_NS, 0);

	return 0;
}
