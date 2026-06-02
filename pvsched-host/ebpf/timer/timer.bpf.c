// SPDX-License-Identifier: GPL-2.0
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include"register_vm_bpf.h"
char LICENSE[] SEC("license") = "GPL";

#define CLOCK_BOOTTIME 7
//timer map and struct
struct elem {
    struct bpf_timer timer;
    __u64 counter;
    __u64 started;
};

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, struct elem);
} timer_map SEC(".maps");



//bpf maps 

struct {
    __uint(type,BPF_MAP_TYPE_HASH);
    __uint(max_entries, 10000000);
    __type(key, char[VM_NAME_LEN]);//vm name
    __type(value,struct vm_t);
} vms SEC(".maps");


//map containing all vcpus
struct {
    __uint(type,BPF_MAP_TYPE_HASH);
    __uint(max_entries, 10000000);
    __type(key, __u32);//thread id
    __type(value,struct vcpu_t);
} vcpus SEC(".maps");



//map containing collection and processing maps






struct {
    __uint(type, BPF_MAP_TYPE_HASH_OF_MAPS);
    __uint(max_entries, 1024);
    __type(key, char[VM_NAME_LEN]);
    __type(value, __u32); // index into inner map array
} map_registry SEC(".maps");


static long callback_fn(struct bpf_map *map, const void *key, void *value, void *ctx);

//timer callbackt
static int timer_cb(void *map, __u32 *key, struct elem *val)
{   

    val->counter++;
    bpf_printk("tick: counter=%llu\n", val->counter);

    //swap collection and processing maps for each vm
    //iterate through each vm 
    long (*cb_p)(struct bpf_map *, const void *, void *, void *) = &callback_fn;
    bpf_for_each_map_elem(&vms, cb_p, NULL, 0);

    bpf_timer_start(&val->timer, 1000000000ULL, 0);
    return 0;
}

static long callback_fn(struct bpf_map *map, const void *key, void *value, void *ctx)
{   
    
    struct vm_t *vm;
    const char *vm_name;
    

    vm_name = (const char *)key;
    vm = (struct vm_t *)value;

    //swap is_collecting for the vm

    if(vm->is_collecting){
        // Currently storing in processing map (1), switch to collection map (0)
        vm->collection_index = 0;
        vm->is_collecting = 0;
    }
    else{
        // Currently storing in collection map (0), switch to processing map (1)
        vm->processing_index = 0;
        vm->is_collecting = 1;
    }
    
    
   
        
    return 0;
}



SEC("tc/ingress")
int tc_prog(struct __sk_buff *skb)
{
    __u32 key = 0;
    struct elem *e = bpf_map_lookup_elem(&timer_map, &key);
    if (!e)
        return 0;

    // First packet starts the timer, all subsequent packets are ignored
    if (__sync_val_compare_and_swap(&e->started, 0ULL, 1ULL) == 0) {
        int ret = bpf_timer_init(&e->timer, &timer_map, CLOCK_BOOTTIME);
        if (ret) {
            bpf_printk("timer_init failed: %d\n", ret);
            __sync_val_compare_and_swap(&e->started, 1ULL, 0ULL);
            return 0;
        }
        bpf_timer_set_callback(&e->timer, timer_cb);
        bpf_timer_start(&e->timer, 1000000000ULL, 0);
        bpf_printk("timer started on first packet\n");
    }
    return 0;
}
