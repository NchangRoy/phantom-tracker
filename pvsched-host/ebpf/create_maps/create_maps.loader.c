#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <bpf/bpf.h>
#include <bpf/libbpf.h>

#include "create_maps.skel.h"

int main()
{
    struct create_maps_bpf *skel;

    int vms_fd;
    int vcpus_fd;

    /* open skeleton only */
    skel = create_maps_bpf__open();
    if (!skel) {
        perror("open skeleton");
        return 1;
    }

    /*
     * Try opening existing pinned maps
     */

    vms_fd = bpf_obj_get("/sys/fs/bpf/vms");
    if (vms_fd >= 0) {
        printf("reusing pinned vms map\n");

        if (bpf_map__reuse_fd(skel->maps.vms, vms_fd) < 0) {
            perror("reuse vms map");
            return 1;
        }
    }

    vcpus_fd = bpf_obj_get("/sys/fs/bpf/vcpus");
    if (vcpus_fd >= 0) {
        printf("reusing pinned vcpus map\n");

        if (bpf_map__reuse_fd(skel->maps.vcpus, vcpus_fd) < 0) {
            perror("reuse vcpus map");
            return 1;
        }
    }

    /*
     * NOW load programs/maps
     */
    if (create_maps_bpf__load(skel) < 0) {
        perror("load skeleton");
        return 1;
    }

    /*
     * attach programs
     */
    if (create_maps_bpf__attach(skel) < 0) {
        perror("attach programs");
        return 1;
    }

    /*
     * Pin maps only if they weren't already pinned
     */

    if (vms_fd < 0) {
        if (bpf_obj_pin(
                bpf_map__fd(skel->maps.vms),
                "/sys/fs/bpf/vms") < 0) {
            perror("pin vms");
            return 1;
        }

        printf("pinned vms map\n");
    }

    if (vcpus_fd < 0) {
        if (bpf_obj_pin(
                bpf_map__fd(skel->maps.vcpus),
                "/sys/fs/bpf/vcpus") < 0) {
            perror("pin vcpus");
            return 1;
        }

        printf("pinned vcpus map\n");
    }

    printf("everything loaded successfully\n");

    return 0;
}