
// include/register_vm_shared.h
#ifndef REGISTER_VM_BPF_H
#define REGISTER_VM_BPF_H

#define VM_NAME_LEN 64

struct vm_t {
	char qmp_socket[128];
	__s64 phantom_count;
	__u32 collection_index;
	__u32 processing_index;
	__u32 is_collecting;
};

struct vcpu_t {
	__u32 vcpu_index;
	char vm_name[VM_NAME_LEN];
};

struct qmp_vpcu {
	int cpuIndex;
	int threadId;
};

#endif