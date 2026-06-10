#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include "phantom_tracker.h"
#include "register_vm_bpf.h"
char LICENSE[] SEC("license") = "GPL";

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
 * Inputs: p - pointer to s32 value
 * Outputs: Returns the decremented value on success, or the value at p if <= 0 or if all retries failed
 * Description: Atomically decrements the value pointed to by p if it is positive.
 *              Uses a compare-and-swap (CAS) loop with up to 8 attempts to handle concurrency.
 */
static inline s32 atomic_dec_if_pos(s32 *p)
{
	for (int attempt = 0; attempt < 8; attempt++) {
		s32 old = *(volatile s32 *)p;
		if (old <= 0) {
			return old;
		}
		if (__sync_val_compare_and_swap(p, old, old - 1) == old) {
			return old - 1;
		}
	}
	return *(volatile s32 *)p;
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
	char collection_buff[VM_NAME_LEN] = {};
	char processing_buff[VM_NAME_LEN] = {};
	int i = 0;
	__u32 idx;
	struct phantom_count count = {};
	void *collection_map_ptr, *processing_map_ptr;

	ts = bpf_ktime_get_ns();

	incoming_process = ctx->next_pid;
	outgoing_process = ctx->prev_pid;

	vcpu = bpf_map_lookup_elem(&vcpus, &incoming_process);

	if (vcpu != NULL) {
		// increment phantom count
		vm = bpf_map_lookup_elem(&vms, vcpu->vm_name);

		// bpf_printk("Entering here because of vcpu %d\n",vcpu->vcpu_index);
		if (vm != NULL) {
			/* Decrement, but clamp at 0.
			 * If phantom_count is 0 the vCPU was already running when
			 * register_vm populated the map, so we missed the initial
			 * outgoing event. Skip the decrement to avoid going negative.
			 */
			atomic_dec_if_pos(&vm->phantom_count);
			// bpf_printk(" %llu ns on cpu %d phantom count %d \n",
			// ts,cpu,vm->phantom_count);

#pragma clang loop unroll(full)
			for (i = 0; i < VM_NAME_LEN - 3; i++) {
				char c = vcpu->vm_name[i];
				collection_buff[i] = c;
				processing_buff[i] = c;
				if (c == '\0')
					break;
			}

			// append "_c" for collection buff
			collection_buff[i] = '_';
			collection_buff[i + 1] = 'c';
			collection_buff[i + 2] = '\0';

			// append "_p" for processing buff
			processing_buff[i] = '_';
			processing_buff[i + 1] = 'p';
			processing_buff[i + 2] = '\0';

			// get the map pointers for the collection and processing buffers
			collection_map_ptr = bpf_map_lookup_elem(
				&map_registry, collection_buff);
			if (!collection_map_ptr) {
				bpf_printk("Error Opening collection buffer\n");
			}

			processing_map_ptr = bpf_map_lookup_elem(
				&map_registry, processing_buff);
			if (!processing_map_ptr) {
				bpf_printk("Error Opening processing buffer\n");
			}

			//decrement phantom count
			if (vm->is_collecting == 1) {
				// use collection map (is_collecting == 1)
				collection_map_ptr = bpf_map_lookup_elem(
					&map_registry, collection_buff);
				if (collection_map_ptr != NULL) {
					idx = vm->collection_index;

					count.timestamp = bpf_ktime_get_ns();
					count.count = vm->phantom_count;

					bpf_map_update_elem(collection_map_ptr,
							    &idx, &count,
							    BPF_ANY);

					__sync_fetch_and_add(
						&vm->collection_index, 1);
				}
			} else {
				// use processing map (is_collecting == 0)
				processing_map_ptr = bpf_map_lookup_elem(
					&map_registry, processing_buff);
				if (processing_map_ptr != NULL) {
					idx = vm->processing_index;

					count.timestamp = bpf_ktime_get_ns();
					count.count = vm->phantom_count;

					bpf_map_update_elem(processing_map_ptr,
							    &idx, &count,
							    BPF_ANY);
					__sync_fetch_and_add(
						&vm->processing_index, 1);
				}
			}
		}
	}

	//logic if vcpu is outgoing

	vcpu = bpf_map_lookup_elem(&vcpus, &outgoing_process);

	if (vcpu != NULL) {
		// increment phantom count
		vm = bpf_map_lookup_elem(&vms, vcpu->vm_name);

		// bpf_printk("Entering here because of vcpu %d\n",vcpu->vcpu_index);
		if (vm != NULL) {
			//bpf_printk("phantom count before increment: %d\n", vm->phantom_count);
			__sync_fetch_and_add(&vm->phantom_count, 1);
			// bpf_printk(" %llu ns on cpu %d phantom count %d \n",
			// ts,cpu,vm->phantom_count);

#pragma clang loop unroll(full)
			for (i = 0; i < VM_NAME_LEN - 3; i++) {
				char c = vcpu->vm_name[i];
				collection_buff[i] = c;
				processing_buff[i] = c;
				if (c == '\0')
					break;
			}

			// append "_c" for collection buff
			collection_buff[i] = '_';
			collection_buff[i + 1] = 'c';
			collection_buff[i + 2] = '\0';

			// append "_p" for processing buff
			processing_buff[i] = '_';
			processing_buff[i + 1] = 'p';
			processing_buff[i + 2] = '\0';

			// get the map pointers for the collection and processing buffers
			collection_map_ptr = bpf_map_lookup_elem(
				&map_registry, collection_buff);
			if (!collection_map_ptr) {
				bpf_printk("Error Opening collection buffer\n");
			}

			processing_map_ptr = bpf_map_lookup_elem(
				&map_registry, processing_buff);
			if (!processing_map_ptr) {
				bpf_printk("Error Opening processing buffer\n");
			}
			//increment phantom count
			if (vm->is_collecting == 1) {
				// use collection map (is_collecting == 1)
				collection_map_ptr = bpf_map_lookup_elem(
					&map_registry, collection_buff);
				if (collection_map_ptr != NULL) {
					idx = vm->collection_index;

					count.timestamp = bpf_ktime_get_ns();
					count.count = vm->phantom_count;

					bpf_map_update_elem(collection_map_ptr,
							    &idx, &count,
							    BPF_ANY);

					__sync_fetch_and_add(
						&vm->collection_index, 1);
				}
			} else {
				// use processing map (is_collecting == 0)
				processing_map_ptr = bpf_map_lookup_elem(
					&map_registry, processing_buff);
				if (processing_map_ptr != NULL) {
					idx = vm->processing_index;

					count.timestamp = bpf_ktime_get_ns();
					count.count = vm->phantom_count;

					bpf_map_update_elem(processing_map_ptr,
							    &idx, &count,
							    BPF_ANY);
					__sync_fetch_and_add(
						&vm->processing_index, 1);
				}
			}
		}
	}

	return 0;
}
