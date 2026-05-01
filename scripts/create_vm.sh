#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Contributors:
#   Human: Himadri Chhaya-Shailesh
#   AI: Claude Sonet 4.6
#
# Usage: create_vm.sh --name <vm-name>

set -euo pipefail

arch="$(uname -m)"
if [[ "$arch" != "x86_64" ]]; then
    echo "error: unsupported host architecture: $arch (x86_64 required)" >&2
    exit 1
fi

. "$(dirname "$0")/lib/args.sh"
. "$(dirname "$0")/lib/numa.sh"
. "$(dirname "$0")/lib/cloudinit.sh"

# --- Dependency check ---
_DEPS_OK=1
check_cmd qemu-system-x86_64 qemu-system-x86 fedora=qemu-kvm rhel=qemu-kvm centos=qemu-kvm almalinux=qemu-kvm rocky=qemu-kvm || _DEPS_OK=0
check_cmd numactl             numactl                                                                                           || _DEPS_OK=0
[[ "$_DEPS_OK" -eq 1 ]] || exit 1

declare_arg name           required "Name of the VM"
declare_arg ssh-pubkey     required "Path to SSH public key file to inject into the VM"
declare_arg password       optional "Password for the debian user (for console login)" debian
declare_arg sockets        optional "Number of CPU sockets" 1
declare_arg cores          optional "Number of cores per socket" 2
declare_arg threads        optional "Number of threads per core" 1
declare_arg mem            optional "RAM size (e.g. 4G)" 4G
declare_arg disk-size      optional "Disk image size (e.g. 20G)" 20G
declare_arg base-image     optional "Path to base cloud image (downloaded if absent)" "debian-12-generic-amd64.qcow2"
declare_arg ssh-port       optional "Host port forwarded to guest SSH (port 22)" 2222
declare_arg add-vsock      optional "Add a vhost-vsock device to the VM" false
declare_arg vsock-cid      optional "Guest CID for vhost-vsock (required when --add-vsock=true)" ""
declare_arg per-vm-cgroups    optional "Use cgroups for the VM" true
declare_arg pin-to-socket     optional "Pin VM to a specific NUMA node" false
declare_arg socket-nr         optional "NUMA node number (required when --pin-to-socket=true)"

parse_args "$@"

if [[ ! "$ARG_NAME" =~ ^[a-zA-Z0-9._-]+$ ]]; then
    echo "error: --name must contain only letters, digits, '.', '_', or '-'" >&2
    exit 1
fi

if [[ "$ARG_ADD_VSOCK" == "true" && -z "${ARG_VSOCK_CID:-}" ]]; then
    echo "error: --vsock-cid is required when --add-vsock is true" >&2
    exit 1
fi

if [[ "$ARG_PIN_TO_SOCKET" == "true" && -z "${ARG_SOCKET_NR:-}" ]]; then
    echo "error: --socket-nr is required when --pin-to-socket is true" >&2
    exit 1
fi

PCPU_LIST=""
if [[ "$ARG_PIN_TO_SOCKET" == "true" ]]; then
    numa_check
    node_count="$(numa_node_count)"
    if [[ "$ARG_SOCKET_NR" -ge "$node_count" ]]; then
        echo "error: --socket-nr=$ARG_SOCKET_NR is out of range (host has $node_count NUMA node(s): 0-$((node_count - 1)))" >&2
        exit 1
    fi
    PCPU_LIST="$(numa_cpulist "$ARG_SOCKET_NR")"
    echo "socket $ARG_SOCKET_NR pCPUs: $PCPU_LIST"
fi

echo "name: $ARG_NAME"
echo "pin-to-socket: $ARG_PIN_TO_SOCKET"
echo "socket-nr: ${ARG_SOCKET_NR:-}"

# --- Disk image ---
DISK_IMG="${ARG_NAME}.qcow2"
SEED_ISO="${ARG_NAME}-seed.iso"

if [[ -f "$DISK_IMG" ]]; then
    echo "disk image $DISK_IMG already exists, booting from it"
    SEED_ISO_ARG=()
else
    cloudinit_check_deps
    if [[ ! -f "$ARG_SSH_PUBKEY" ]]; then
        echo "error: SSH public key file not found: $ARG_SSH_PUBKEY" >&2
        exit 1
    fi
    SSH_PUBKEY_STR="$(cat "$ARG_SSH_PUBKEY")"
    cloudinit_fetch_base_image "$ARG_BASE_IMAGE"
    cloudinit_create_overlay   "$ARG_BASE_IMAGE" "$DISK_IMG" "$ARG_DISK_SIZE"
    cloudinit_make_iso         "$SEED_ISO" "$ARG_NAME" "$SSH_PUBKEY_STR" "$ARG_PASSWORD"
    SEED_ISO_ARG=(-drive "file=$SEED_ISO,format=raw,if=virtio,readonly=on")
fi

# --- CPU topology ---
TOTAL_VCPUS=$(( ARG_SOCKETS * ARG_CORES * ARG_THREADS ))
CPU_TOPOLOGY="$TOTAL_VCPUS,sockets=$ARG_SOCKETS,cores=$ARG_CORES,threads=$ARG_THREADS"

# --- QEMU command ---
QEMU_CMD=(
    qemu-system-x86_64
    -enable-kvm
    -cpu host
    -name "guest=$ARG_NAME,debug-threads=on"
    -m "$ARG_MEM"
    -smp "$CPU_TOPOLOGY"
    -drive "file=$DISK_IMG,format=qcow2,if=virtio"
    "${SEED_ISO_ARG[@]}"
    -net nic,model=virtio
    -net "user,hostfwd=tcp::${ARG_SSH_PORT}-:22"
    -nographic
)

if [[ "$ARG_ADD_VSOCK" == "true" ]]; then
    QEMU_CMD+=(-device "vhost-vsock-pci,guest-cid=${ARG_VSOCK_CID}")
fi

if [[ "$ARG_PIN_TO_SOCKET" == "true" ]]; then
    QEMU_CMD=(numactl --physcpubind="$PCPU_LIST" --membind="$ARG_SOCKET_NR" "${QEMU_CMD[@]}")
fi

# --- per-VM cgroup (systemd transient scope, machine.slice — same layout as libvirt) ---
if [[ "$ARG_PER_VM_CGROUPS" == "true" ]]; then
    if ! command -v systemd-run &>/dev/null; then
        echo "warning: systemd-run not found, skipping per-vm cgroup" >&2
    else
        QEMU_CMD=(
            systemd-run
            --scope
            --collect
            --quiet
            --slice=machine.slice
            --unit="qemu-$ARG_NAME"
            --
            "${QEMU_CMD[@]}"
        )
        echo "cgroup: machine.slice/qemu-$ARG_NAME.scope"
    fi
fi

echo "launching VM:"
printf '  %q' "${QEMU_CMD[@]}"
printf '\n'
echo "once booted, SSH in with: ssh -p $ARG_SSH_PORT debian@localhost"
if [[ -n "${SEED_ISO_ARG[*]}" ]]; then
    echo "note: first boot runs cloud-init, SSH may take 1-2 minutes to become available"
fi
echo "starting in 10 seconds..."
sleep 10
exec "${QEMU_CMD[@]}"
