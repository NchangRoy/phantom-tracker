#include"vmlinux.h"
#include<bpf/bpf_helpers.h>


#include"register_vm_bpf.h"

char LICENSE[] SEC("license")= "GPL";




struct {
    __uint(type,BPF_MAP_TYPE_HASH);
    __uint(max_entries, 10000000);
    __type(key, char[64]);//vm name
    __type(value,struct vm_t);
} vms SEC(".maps");

struct {
    __uint(type,BPF_MAP_TYPE_HASH);
    __uint(max_entries, 10000000);
    __type(key, __u32);//thread id
    __type(value,struct vcpu_t);
} vcpus SEC(".maps");



//for debuging purposes





