#ifndef CREATE_MAPS_H
#define CREATE_MAPS_H

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <bpf/bpf.h>

#include "linkedlist.h"

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

void register_vm(struct Node *vcpus, char *vm_name, char *qmp_socket)
{
    struct vm_t *vm = malloc(sizeof(struct vm_t));
    if (!vm) {
        perror("malloc vm failed");
        exit(1);
    }

    memset(vm, 0, sizeof(*vm));

    strncpy(vm->qmp_socket, qmp_socket, sizeof(vm->qmp_socket) - 1);
    vm->phantom_count = 0;

    int vm_fd = bpf_obj_get("/sys/fs/bpf/vms");
    int vcpus_fd = bpf_obj_get("/sys/fs/bpf/vcpus");

    if (vm_fd < 0 || vcpus_fd < 0) {
        perror("bpf_obj_get failed");
        exit(1);
    }

    if (bpf_map_update_elem(vm_fd, vm_name, vm, BPF_ANY)) {
        perror("Error saving vm details to vms map");
        exit(1);
    }
    //exclude head
    Node *temp = vcpus->next;

    while (temp != NULL) {

        struct qmp_vpcu *qmp_detail =  (struct qmp_vpcu *)temp->data;

        struct vcpu_t *vcpu = malloc(sizeof(struct vcpu_t));
        if (!vcpu) {
            perror("malloc vcpu failed");
            exit(1);
        }
        printf(" cpu index  %d",qmp_detail->cpuIndex);
        vcpu->vcpu_index = qmp_detail->cpuIndex;
       

        memset(vcpu->vm_name, 0, sizeof(vcpu->vm_name));
        strncpy(vcpu->vm_name, vm_name, sizeof(vcpu->vm_name) - 1);

        __u32 key = qmp_detail->threadId;
        printf("key,%d\n",key);
        if (bpf_map_update_elem(vcpus_fd, &key, vcpu, BPF_ANY)) {
            perror("Error saving vcpu details to vcpus map");
            exit(1);
        }

        temp = temp->next;
    }
}

#endif