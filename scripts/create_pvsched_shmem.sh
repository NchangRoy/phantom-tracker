#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Contributors:
#   Human: Himadri Chhaya-Shailesh
#   AI: Claude Sonet 4.6, ChatGPT-5.5
#
# Build and load the host ivshmem kernel module used by Phantom Tracker.

set -euo pipefail

# shellcheck source=lib/distro.sh
. "$(dirname "$0")/lib/distro.sh"

create_pvsched_shmem_check_deps() {
    local missing=0

    if ! command -v make &>/dev/null; then
        echo "error: required tool not found: make" >&2
        echo "  install it with:" >&2
        distro_install_hint make fedora=make rhel=make centos=make almalinux=make rocky=make arch=make alpine=make gentoo=sys-devel/make nixos=gnumake >&2
        missing=1
    fi

    if [[ ! -e "/lib/modules/$(uname -r)/build" ]]; then
        echo "error: required kernel headers are missing for running kernel: $(uname -r)" >&2
        echo "  install them with:" >&2
        distro_install_hint "linux-headers-$(uname -r)" fedora="kernel-devel-$(uname -r)" rhel="kernel-devel-$(uname -r)" centos="kernel-devel-$(uname -r)" almalinux="kernel-devel-$(uname -r)" rocky="kernel-devel-$(uname -r)" opensuse="kernel-devel" sles="kernel-devel" arch=linux-headers alpine=linux-headers gentoo=sys-kernel/linux-headers nixos=linuxHeaders >&2
        missing=1
    fi

    return "$missing"
}

create_pvsched_shmem_setup() {
    local scripts_dir host_dir module_ko

    scripts_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
    host_dir="$scripts_dir/../pvsched-shmem/host"
    module_ko="$host_dir/host_ivshmem.ko"

    create_pvsched_shmem_check_deps

    if [[ ! -d "$host_dir" ]]; then
        echo "error: host module directory not found: $host_dir" >&2
        return 1
    fi

    echo "building host ivshmem kernel module in $host_dir"
    make -C "$host_dir"

    if [[ ! -f "$module_ko" ]]; then
        echo "error: expected kernel module not found after build: $module_ko" >&2
        return 1
    fi

    if grep -qE '^host_ivshmem[[:space:]]' /proc/modules; then
        echo "host_ivshmem kernel module already loaded"
    else
        echo "installing host_ivshmem kernel module"
        if [[ "$EUID" -eq 0 ]]; then
            make -C "$host_dir" modules_install
            depmod -a
        else
            sudo make -C "$host_dir" modules_install
            sudo depmod -a
        fi

        echo "loading host_ivshmem kernel module"
        if [[ "$EUID" -eq 0 ]]; then
            modprobe host_ivshmem
        else
            sudo modprobe host_ivshmem
        fi
    fi
}

create_pvsched_shmem_cleanup() {
    if ! grep -qE '^host_ivshmem[[:space:]]' /proc/modules; then
        return 0
    fi

    echo "unloading host_ivshmem kernel module"
    if [[ "$EUID" -eq 0 ]]; then
        modprobe -r host_ivshmem
    else
        sudo modprobe -r host_ivshmem
    fi

    if grep -qE '^host_ivshmem[[:space:]]' /proc/modules; then
        echo "error: failed to unload host_ivshmem kernel module" >&2
        return 1
    fi

    if [[ -e /dev/host_ivshmem0 ]]; then
        echo "warning: /dev/host_ivshmem0 still exists after module unload" >&2
    else
        echo "removed host backend device: /dev/host_ivshmem0"
    fi
}

if [[ "${BASH_SOURCE[0]}" == "$0" ]]; then
    create_pvsched_shmem_setup
fi
