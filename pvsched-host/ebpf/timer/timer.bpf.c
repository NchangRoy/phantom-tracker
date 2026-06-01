// SPDX-License-Identifier: GPL-2.0
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>

char LICENSE[] SEC("license") = "GPL";

#define CLOCK_BOOTTIME 7

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

static int timer_cb(void *map, __u32 *key, struct elem *val)
{
    val->counter++;
    bpf_printk("tick: counter=%llu\n", val->counter);
    bpf_timer_start(&val->timer, 1000000000ULL, 0);
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
