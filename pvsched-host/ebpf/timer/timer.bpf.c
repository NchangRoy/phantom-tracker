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
    char collection_buff[VM_NAME_LEN] = {};
    char processing_buff[VM_NAME_LEN] = {};
    int i = 0;
    struct vm_t *vm;
    const char *vm_name;
    void *collection_map_ptr, *processing_map_ptr;

    vm_name = (const char *)key;
    vm = (struct vm_t *)value;

    // copy vm_name safely
    #pragma clang loop unroll(full)
    for (i = 0; i < VM_NAME_LEN - 3; i++) {
        char c = vm_name[i];
        collection_buff[i] = c;
        processing_buff[i] = c;
        if (c == '\0')
            break;
    }

    // append "_c" for collection buff
    collection_buff[i] = '_';
    collection_buff[i+1] = 'c';
    collection_buff[i+2] = '\0';

    // append "_p" for processing buff
    processing_buff[i] = '_';
    processing_buff[i+1] = 'p';
    processing_buff[i+2] = '\0';

    // get the map pointers for the collection and processing buffers
    collection_map_ptr = bpf_map_lookup_elem(&map_registry, collection_buff);
    if (!collection_map_ptr) {
        bpf_printk("Error Opening collection buffer\n");
    }

    processing_map_ptr = bpf_map_lookup_elem(&map_registry, processing_buff);
    if (!processing_map_ptr) {
        bpf_printk("Error Opening processing buffer\n");
    }

    // swap the map entries in the map_registry (if supported)
    if (collection_map_ptr && processing_map_ptr) {
        bpf_map_update_elem(&map_registry, processing_buff, &collection_map_ptr, BPF_ANY);
        bpf_map_update_elem(&map_registry, collection_buff, &processing_map_ptr, BPF_ANY);
    }

    // set the processing index to the collection index and reinitialize the collection index
    vm->processing_index = vm->collection_index;
    vm->collection_index = 0;
        
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
