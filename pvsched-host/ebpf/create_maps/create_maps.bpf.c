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

SEC("tp/sched/sched_switch")
int handle_switch(struct trace_event_raw_sched_switch *ctx) {

  __u32 cpu;
  __u32 tid;
  __u64 ts;
  struct vcpu_t *vcpu;
  struct vm_t *vm;
  char collection_buff[VM_NAME_LEN] = {};
  char processing_buff[VM_NAME_LEN] = {};
  int i = 0;
  __u32 idx;
  struct phantom_count count = {};
  void *collection_map_ptr, *processing_map_ptr;

  cpu = bpf_get_smp_processor_id();
  ts = bpf_ktime_get_ns();

  tid = ctx->next_pid;

  vcpu = bpf_map_lookup_elem(&vcpus, &tid);

  if (vcpu != NULL) {

    // increment phantom count
    vm = bpf_map_lookup_elem(&vms, vcpu->vm_name);

    // bpf_printk("Entering here because of vcpu %d\n",vcpu->vcpu_index);
    if (vm != NULL) {
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
      collection_map_ptr = bpf_map_lookup_elem(&map_registry, collection_buff);
      if (!collection_map_ptr) {
        bpf_printk("Error Opening collection buffer\n");
      }

      processing_map_ptr = bpf_map_lookup_elem(&map_registry, processing_buff);
      if (!processing_map_ptr) {
        bpf_printk("Error Opening processing buffer\n");
      }

      if (vm->is_collecting == 1) {
        // use collection map (is_collecting == 1)
        collection_map_ptr =
            bpf_map_lookup_elem(&map_registry, collection_buff);
        if (collection_map_ptr != NULL) {
          idx = vm->collection_index;
          

          count.timestamp = bpf_ktime_get_ns();
          count.count = vm->phantom_count;

          bpf_map_update_elem(collection_map_ptr, &idx, &count, BPF_ANY);

          __sync_fetch_and_add(&vm->collection_index, 1);
        }
      } else {
        // use processing map (is_collecting == 0)
        processing_map_ptr =
            bpf_map_lookup_elem(&map_registry, processing_buff);
        if (processing_map_ptr != NULL) {
          idx = vm->processing_index;
          

          count.timestamp = bpf_ktime_get_ns();
          count.count = vm->phantom_count;

          bpf_map_update_elem(processing_map_ptr, &idx, &count, BPF_ANY);
          __sync_fetch_and_add(&vm->processing_index, 1);
        }
      }
    }
  }

  return 0;
}

SEC("tp/sched/sched_wakeup")
int handle_wakeup(struct trace_event_raw_sched_wakeup_template *ctx) {

  __u32 cpu;
  __u32 tid;
  __u64 ts;
  struct vcpu_t *vcpu;
  struct vm_t *vm;
  char collection_buff[VM_NAME_LEN] = {};
  char processing_buff[VM_NAME_LEN] = {};
  int i = 0;
  __u32 idx;
  struct phantom_count count = {};
  void *collection_map_ptr, *processing_map_ptr;

  cpu = bpf_get_smp_processor_id();
  ts = bpf_ktime_get_ns();

  tid = ctx->pid;

  vcpu = bpf_map_lookup_elem(&vcpus, &tid);

  if (vcpu != NULL) {
    // decrement phantom count
    vm = bpf_map_lookup_elem(&vms, vcpu->vm_name);

    if (vm != NULL) {
      __sync_fetch_and_add(&vm->phantom_count, -1);

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

      if (vm->is_collecting == 1) {
        // use collection map (is_collecting == 1)
        collection_map_ptr =
            bpf_map_lookup_elem(&map_registry, collection_buff);
        if (collection_map_ptr != NULL) {
          idx = vm->collection_index;
          __sync_fetch_and_add(&vm->collection_index, 1);

          count.timestamp = bpf_ktime_get_ns();
          count.count = vm->phantom_count;

          bpf_map_update_elem(collection_map_ptr, &idx, &count, BPF_ANY);
        }
      } else {
        // use processing map (is_collecting == 0)
        processing_map_ptr =
            bpf_map_lookup_elem(&map_registry, processing_buff);
        if (processing_map_ptr != NULL) {
          idx = vm->processing_index;
          __sync_fetch_and_add(&vm->processing_index, 1);

          count.timestamp = bpf_ktime_get_ns();
          count.count = vm->phantom_count;

          bpf_map_update_elem(processing_map_ptr, &idx, &count, BPF_ANY);
        }
      }
    }
  }

  return 0;
}
