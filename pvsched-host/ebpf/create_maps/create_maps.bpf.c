#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include "phantom_tracker.h"
#include "register_vm_bpf.h"

char LICENSE[] SEC("license") = "GPL";

#define TASK_RUNNING 0x00000000
#define TASK_WAKING 0x00000200

// map containing all vms
struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 10000000);
	__type(key, char[VM_NAME_LEN]); // vm name
	__type(value, struct vm_t);
} vms SEC(".maps");

// map containing all vcpus
struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 10000000);
	__type(key, __u32); // thread id
	__type(value, struct vcpu_t);
} vcpus SEC(".maps");

// map containing collection and processing maps
struct {
	__uint(type, BPF_MAP_TYPE_HASH_OF_MAPS);
	__uint(max_entries, 1024);
	__type(key, char[VM_NAME_LEN]);
	__type(value, __u32); // index into inner map array
} map_registry SEC(".maps");

/*
 * Inputs: p - pointer to s64 value
 * Outputs: Returns the decremented value on success, or the value at p if <= 0 or if all retries failed
 * Description: Atomically decrements the value pointed to by p if it is positive.
 *              Uses BPF 64-bit atomic compare-and-swap to ensure correct field-level atomicity.
 */
static inline s64 atomic_dec_if_pos(s64 *p)
{
	for (int attempt = 0; attempt < 24; attempt++) {
		s64 old = *(volatile s64 *)p;
		if (old <= 0) {
			return old;
		}
		s64 new = old - 1;
		if (__sync_val_compare_and_swap(p, old, new) == old) {
			return new;
		}
	}
	return *(volatile s64 *)p;
}

/*
 * Inputs: p - pointer to s64 value (vm->phantom_count)
		   vCPUs - pointer to u32 value (vm->vcpu_count)
 * Outputs: Returns the decremented value on success, or the value at p if <= 0 or if all retries failed
 * Description: Atomically decrements the value pointed to by p if it is positive.
 *              Uses BPF 64-bit atomic compare-and-swap to ensure correct field-level atomicity.
 */
static inline s64 atomic_inc_if_lt_ceil(s64 *p, u32 *ceil_val)
{
	for (int attempt = 0; attempt < 24; attempt++) {
		s64 old = *(volatile s64 *)p;

		if (old >= *ceil_val) {
			return old;
		}
		s64 new = old + 1;
		if (__sync_val_compare_and_swap(p, old, new) == old) {
			return new;
		}
	}
	return *(volatile s64 *)p;
}

/*
 * Inputs: ctx - context containing sched_switch tracepoint data including prev and next pids
 * Outputs: Returns 0
 * Description: Tracepoint handler for sched_switch. Increments phantom count on outgoing vCPUs and decrements on incoming vCPUs, records timestamps and updates collection/processing buffers
 */
SEC("tp/sched/sched_switch")
int handle_switch(struct trace_event_raw_sched_switch *ctx)
{
	__u32 cpu;
	__u32 incoming_process, outgoing_process;
	__u64 ts;
	struct vcpu_t *vcpu;
	struct vm_t *vm;
	int i;
	__u32 idx;
	void *collection_map_ptr, *processing_map_ptr;
	s64 new_count;
	u64 prev_state;

	ts = bpf_ktime_get_ns();

	incoming_process = ctx->next_pid;
	outgoing_process = ctx->prev_pid;

	vcpu = bpf_map_lookup_elem(&vcpus, &incoming_process);

	if (vcpu != NULL) {
		prev_state = ctx->prev_state;

		if (__sync_bool_compare_and_swap(&vcpu->is_phantom, 1, 0)) {
			// Declare buffers here (before lookup) so their zero-init doesn't
			// spill vm between the null check and its first use, which would
			// cause the verifier to lose the map_value_or_null -> map_value proof.
			char collection_buff_1[VM_NAME_LEN] = {};
			char collection_buff_2[VM_NAME_LEN] = {};
			struct phantom_count count = {};

			// decrement phantom count
			vm = bpf_map_lookup_elem(&vms, vcpu->vm_name);

			if (vm != NULL) {
				/* Decrement, but clamp at 0.
				 * If phantom_count is 0 the vCPU was already running when
				 * register_vm populated the map, so we missed the initial
				 * outgoing event. Skip the decrement to avoid going negative.
				 */
				new_count =
					atomic_dec_if_pos(&vm->phantom_count);

#pragma GCC unroll 64
				for (i = 0; i < VM_NAME_LEN - 3; i++) {
					char c = vcpu->vm_name[i];
					collection_buff_1[i] = c;
					collection_buff_2[i] = c;
					if (c == '\0')
						break;
				}

				// append "_1" for collection buff 1
				collection_buff_1[i] = '_';
				collection_buff_1[i + 1] = '1';
				collection_buff_1[i + 2] = '\0';

				// append "_2" for collection buff 2
				collection_buff_2[i] = '_';
				collection_buff_2[i + 1] = '2';
				collection_buff_2[i + 2] = '\0';

				// get the map pointers for the collection and processing buffers
				collection_map_ptr = bpf_map_lookup_elem(
					&map_registry, collection_buff_1);
				if (!collection_map_ptr) {
					bpf_printk(
						"Error Opening collection buffer\n");
				}

				processing_map_ptr = bpf_map_lookup_elem(
					&map_registry, collection_buff_2);
				if (!processing_map_ptr) {
					bpf_printk(
						"Error Opening processing buffer\n");
				}

				// Plain field read — vm is map_value (non-null) here
				__u64 collecting = vm->is_collectx_in_buff_1;
				if (collecting == 1) {
					// use collection map (is_collectx_in_buff_1 == 1)
					collection_map_ptr =
						bpf_map_lookup_elem(
							&map_registry,
							collection_buff_1);
					if (collection_map_ptr != NULL) {
						idx = __sync_fetch_and_add(
							&vm->collection_buff_1_index,
							1);

						count.timestamp =
							bpf_ktime_get_ns();
						count.count = new_count;

						bpf_map_update_elem(
							collection_map_ptr,
							&idx, &count, BPF_ANY);

						bpf_printk(
							"Updating collection dec map with count %lld at index %u\n",
							count.count, idx);
					}
				} else {
					// use processing map (is_collectx_in_buff_1 == 0)
					processing_map_ptr =
						bpf_map_lookup_elem(
							&map_registry,
							collection_buff_2);
					if (processing_map_ptr != NULL) {
						idx = __sync_fetch_and_add(
							&vm->collection_buff_2_index,
							1);

						count.timestamp =
							bpf_ktime_get_ns();
						count.count = new_count;

						bpf_map_update_elem(
							processing_map_ptr,
							&idx, &count, BPF_ANY);

						bpf_printk(
							"Updating processing dec map with count %lld at index %u\n",
							count.count, idx);
					}
				}
			}
		}
	}

	//logic if vcpu is outgoing
	vcpu = bpf_map_lookup_elem(&vcpus, &outgoing_process);
	prev_state = ctx->prev_state;
	if (vcpu != NULL) {
		// Only mark as phantom when vCPU was preempted (TASK_RUNNING) — not on voluntary sleep
		if (prev_state == TASK_RUNNING || prev_state == TASK_WAKING) {
			if (__sync_bool_compare_and_swap(&vcpu->is_phantom, 0, 1)) {
				// Declare buffers before lookup — same spill-prevention as incoming block
				char collection_buff_1[VM_NAME_LEN] = {};
				char collection_buff_2[VM_NAME_LEN] = {};
				struct phantom_count count = {};

				// increment phantom count
				vm = bpf_map_lookup_elem(&vms, vcpu->vm_name);

				if (vm != NULL) {
					new_count = atomic_inc_if_lt_ceil(
						&vm->phantom_count,
						&vm->nb_vcpus);

#pragma GCC unroll 64
					for (i = 0; i < VM_NAME_LEN - 3; i++) {
						char c = vcpu->vm_name[i];
						collection_buff_1[i] = c;
						collection_buff_2[i] = c;
						if (c == '\0')
							break;
					}

					// append "_1" for collection buff 1
					collection_buff_1[i] = '_';
					collection_buff_1[i + 1] = '1';
					collection_buff_1[i + 2] = '\0';

					// append "_2" for collection buff 2
					collection_buff_2[i] = '_';
					collection_buff_2[i + 1] = '2';
					collection_buff_2[i + 2] = '\0';

					// get the map pointers for the collection and processing buffers
					collection_map_ptr =
						bpf_map_lookup_elem(
							&map_registry,
							collection_buff_1);
					if (!collection_map_ptr) {
						bpf_printk(
							"Error Opening collection buffer\n");
					}

					processing_map_ptr =
						bpf_map_lookup_elem(
							&map_registry,
							collection_buff_2);
					if (!processing_map_ptr) {
						bpf_printk(
							"Error Opening processing buffer\n");
					}

					// Plain field read — vm is map_value (non-null) here
					__u64 collecting = vm->is_collectx_in_buff_1;
					if (collecting == 1) {
						// use collection map (is_collectx_in_buff_1 == 1)
						collection_map_ptr =
							bpf_map_lookup_elem(
								&map_registry,
								collection_buff_1);
						if (collection_map_ptr !=  NULL) {
							idx = __sync_fetch_and_add(
								&vm->collection_buff_1_index,
								1);

							count.timestamp =								bpf_ktime_get_ns();
							count.count = new_count;

							bpf_map_update_elem(
								collection_map_ptr,
								&idx, &count,
								BPF_ANY);

							bpf_printk(
								"Updating collection map inc with count %lld at index %u\n",
								count.count,
								idx);
						}
					} else {
						// use processing map (is_collectx_in_buff_1 == 0)
						processing_map_ptr =
							bpf_map_lookup_elem(
								&map_registry,
								collection_buff_2);
						if (processing_map_ptr !=
						    NULL) {
							idx = __sync_fetch_and_add(
								&vm->collection_buff_2_index,
								1);

							count.timestamp =
								bpf_ktime_get_ns();
							count.count = new_count;

							bpf_map_update_elem(
								processing_map_ptr,
								&idx, &count,
								BPF_ANY);

							bpf_printk(
								"Updating processing map inc with count %lld at index %u\n",
								count.count,
								idx);
						}
					}
				}
			}
		}
	}
	return 0;
}
