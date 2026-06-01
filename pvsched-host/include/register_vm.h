// SPDX-License-Identifier: GPL-2.0
#ifndef CREATE_MAPS_H
#define CREATE_MAPS_H
 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
 
#include <bpf/bpf.h>
#include <bpf/libbpf.h>
 
#include "linkedlist.h"
#include "phantom_tracker.h"
#include "register_vm_bpf.h"
 
#define PIN_BASE		"/sys/fs/bpf"
#define PIN_VMS			PIN_BASE "/vms"
#define PIN_VCPUS		PIN_BASE "/vcpus"
#define PIN_REGISTRY		PIN_BASE "/map_registry"
 
#define VM_MAP_MAX_ENTRIES	1000000
#define VM_KEY_SUFFIX_C		"_c"
#define VM_KEY_SUFFIX_P		"_p"
 
/*
 * register_vm - write a VM and its vCPUs into the pinned BPF maps.
 *
 * Returns 0 on success, -1 on error.
 */
static inline int register_vm(struct Node *vcpus, const char *vm_name,
			       const char *qmp_socket)
{
	struct vm_t *vm;
	char vm_key[64];
	int vm_fd = -1;
	int vcpus_fd = -1;
	int ret = -1;
	Node *temp;
 
	vm = malloc(sizeof(*vm));
	if (!vm) {
		perror("malloc vm");
		return -1;
	}
	memset(vm, 0, sizeof(*vm));
 
	strncpy(vm->qmp_socket, qmp_socket, sizeof(vm->qmp_socket) - 1);
	vm->phantom_count    = 0;
	vm->collection_index = 0;
	vm->processing_index = 0;
 
	vm_fd = bpf_obj_get(PIN_VMS);
    if (vm_fd < 0) {
		perror("bpf_obj_get vms");
		goto cleanup_vm;
	}
 
	vcpus_fd = bpf_obj_get(PIN_VCPUS);
	if (vcpus_fd < 0) {
		perror("bpf_obj_get vcpus");
		goto cleanup_vm_fd;
	}
 
	memset(vm_key, 0, sizeof(vm_key));
	strncpy(vm_key, vm_name, sizeof(vm_key) - 1);
 
	if (bpf_map_update_elem(vm_fd, vm_key, vm, BPF_ANY) < 0) {
		perror("bpf_map_update_elem vms");
		goto cleanup_vcpus_fd;
	}
 
	/* skip dummy head node */
	temp = vcpus->next;
	while (temp) {
		struct qmp_vpcu *qmp_detail = (struct qmp_vpcu *)temp->data;
		struct vcpu_t *vcpu;
		__u32 key;
 
		vcpu = malloc(sizeof(*vcpu));
		if (!vcpu) {
			perror("malloc vcpu");
			goto cleanup_vcpus_fd;
		}
 
		memset(vcpu->vm_name, 0, sizeof(vcpu->vm_name));
		strncpy(vcpu->vm_name, vm_name, sizeof(vcpu->vm_name) - 1);
		vcpu->vcpu_index = qmp_detail->cpuIndex;
 
		key = (__u32)qmp_detail->threadId;
 
		printf("registering cpu_index=%d thread_id=%u\n",
		       qmp_detail->cpuIndex, key);
 
		if (bpf_map_update_elem(vcpus_fd, &key, vcpu, BPF_ANY) < 0) {
			perror("bpf_map_update_elem vcpus");
			free(vcpu);
			goto cleanup_vcpus_fd;
		}
 
		free(vcpu);
		temp = temp->next;
	}
 
	ret = 0;
 
cleanup_vcpus_fd:
	close(vcpus_fd);
cleanup_vm_fd:
	close(vm_fd);
cleanup_vm:
	free(vm);
	return ret;
}
 
/*
 * setup_vm_maps - create and pin per-VM collection and processing BPF maps,
 * then register them in the global map_registry.
 *
 * Returns 0 on success, -1 on error.
 */
static inline int setup_vm_maps(const char *vm_name)
{
	char dir_path[128];
	char pin_path[128];
	char key[64];
	int registry_fd = -1;
	int collection  = -1;
	int processing  = -1;
	__u32 col_fd;
	__u32 proc_fd;
	int ret = -1;
 
	registry_fd = bpf_obj_get(PIN_REGISTRY);
	if (registry_fd < 0) {
		perror("bpf_obj_get map_registry");
		return -1;
	}
 
	/* create per-VM bpffs directory */
	snprintf(dir_path, sizeof(dir_path), PIN_BASE "/%s", vm_name);
	if (mkdir(dir_path, 0755) < 0 && errno != EEXIST) {
		perror("mkdir vm dir");
		goto cleanup_registry;
	}
 
	/* collection map */
	collection = bpf_map_create(BPF_MAP_TYPE_ARRAY,
				    "collection",
				    sizeof(__u32),
				    sizeof(struct phantom_count),
				    VM_MAP_MAX_ENTRIES,
				    NULL);
	if (collection < 0) {
		perror("bpf_map_create collection");
		goto cleanup_registry;
	}
 
	snprintf(pin_path, sizeof(pin_path), PIN_BASE "/%s/collection", vm_name);
	if (bpf_obj_pin(collection, pin_path) < 0) {
		perror("bpf_obj_pin collection");
		goto cleanup_collection;
	}
 
	/* processing map */
	processing = bpf_map_create(BPF_MAP_TYPE_ARRAY,
				    "processing",
				    sizeof(__u32),
				    sizeof(struct phantom_count),
				    VM_MAP_MAX_ENTRIES,
				    NULL);
	if (processing < 0) {
		perror("bpf_map_create processing");
		goto cleanup_collection;
	}
 
	snprintf(pin_path, sizeof(pin_path), PIN_BASE "/%s/processing", vm_name);
	if (bpf_obj_pin(processing, pin_path) < 0) {
		perror("bpf_obj_pin processing");
		goto cleanup_processing;
	}
 
	/* register collection map in registry */
	memset(key, 0, sizeof(key));
	snprintf(key, sizeof(key), "%s" VM_KEY_SUFFIX_C, vm_name);
	col_fd = (__u32)collection;
 
	if (bpf_map_update_elem(registry_fd, key, &col_fd, BPF_ANY) < 0) {
		fprintf(stderr, "bpf_map_update_elem collection registry: %s\n",
			strerror(errno));
		goto cleanup_processing;
	}
 
	/* register processing map in registry */
	memset(key, 0, sizeof(key));
	snprintf(key, sizeof(key), "%s" VM_KEY_SUFFIX_P, vm_name);
	proc_fd = (__u32)processing;
 
	if (bpf_map_update_elem(registry_fd, key, &proc_fd, BPF_ANY) < 0) {
		fprintf(stderr, "bpf_map_update_elem processing registry: %s\n",
			strerror(errno));
		goto cleanup_processing;
	}
 
	ret = 0;
 
cleanup_processing:
	close(processing);
cleanup_collection:
	close(collection);
cleanup_registry:
	close(registry_fd);
	return ret;
}
 
#endif /* CREATE_MAPS_H */
 