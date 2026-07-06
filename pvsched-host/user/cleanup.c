// SPDX-License-Identifier: GPL-2.0

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>

#include <bpf/bpf.h>
#include <bpf/libbpf.h>

#include "register_vm.h"

int main(int argc, char *argv[])
{
	if (argc < 2) {
		fprintf(stderr, "Usage: %s <vm_name>\n", argv[0]);
		return 1;
	}

	const char *vm_name = argv[1];
	char path[256];
	int ret = 0;

	printf("Cleaning up resources for VM: %s\n", vm_name);

	// 1. Unlink collection_buff_1
	snprintf(path, sizeof(path), "%s/%s/collection_buff_1", PIN_BASE, vm_name);
	if (unlink(path) < 0) {
		if (errno != ENOENT) {
			perror("unlink collection_buff_1");
			ret = 1;
		}
	} else {
		printf("Unpinned: %s\n", path);
	}

	// 2. Unlink collection_buff_2
	snprintf(path, sizeof(path), "%s/%s/collection_buff_2", PIN_BASE, vm_name);
	if (unlink(path) < 0) {
		if (errno != ENOENT) {
			perror("unlink collection_buff_2");
			ret = 1;
		}
	} else {
		printf("Unpinned: %s\n", path);
	}

	// 3. Remove per-VM BPF directory
	snprintf(path, sizeof(path), "%s/%s", PIN_BASE, vm_name);
	if (rmdir(path) < 0) {
		if (errno != ENOENT) {
			perror("rmdir VM directory");
			ret = 1;
		}
	} else {
		printf("Removed directory: %s\n", path);
	}

	// 4. Delete entries from registry map
	int registry_fd = bpf_obj_get(PIN_REGISTRY);
	if (registry_fd >= 0) {
		char key[64];

		snprintf(key, sizeof(key), "%s" VM_KEY_SUFFIX_1, vm_name);
		if (bpf_map_delete_elem(registry_fd, key) < 0) {
			if (errno != ENOENT) {
				perror("bpf_map_delete_elem registry suffix 1");
				ret = 1;
			}
		} else {
			printf("Deleted registry key: %s\n", key);
		}

		snprintf(key, sizeof(key), "%s" VM_KEY_SUFFIX_2, vm_name);
		if (bpf_map_delete_elem(registry_fd, key) < 0) {
			if (errno != ENOENT) {
				perror("bpf_map_delete_elem registry suffix 2");
				ret = 1;
			}
		} else {
			printf("Deleted registry key: %s\n", key);
		}

		close(registry_fd);
	} else {
		if (errno != ENOENT) {
			perror("bpf_obj_get map_registry");
			ret = 1;
		}
	}

	// 5. Delete entry from VMs map
	int vm_fd = bpf_obj_get(PIN_VMS);
	if (vm_fd >= 0) {
		if (bpf_map_delete_elem(vm_fd, vm_name) < 0) {
			if (errno != ENOENT) {
				perror("bpf_map_delete_elem vms");
				ret = 1;
			}
		} else {
			printf("Deleted VM key from vms map: %s\n", vm_name);
		}
		close(vm_fd);
	} else {
		if (errno != ENOENT) {
			perror("bpf_obj_get vms");
			ret = 1;
		}
	}

	// 6. Delete all associated vCPUs from vCPUs map
	int vcpus_fd = bpf_obj_get(PIN_VCPUS);
	if (vcpus_fd >= 0) {
		cleanup_stale_vcpus(vcpus_fd, vm_name);
		printf("Cleaned up vCPUs for VM: %s\n", vm_name);
		close(vcpus_fd);
	} else {
		if (errno != ENOENT) {
			perror("bpf_obj_get vcpus");
			ret = 1;
		}
	}

	return ret;
}
