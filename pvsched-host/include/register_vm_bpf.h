// include/register_vm_shared.h
#ifndef REGISTER_VM_BPF_H
#define REGISTER_VM_BPF_H


struct vm_t {
    char qmp_socket[128];
    __u32 phantom_count;
};

struct vcpu_t {
    __u32 vcpu_index;
    char vm_name[60];
};

struct qmp_vpcu {
    int cpuIndex;
    int threadId;
};

#endif