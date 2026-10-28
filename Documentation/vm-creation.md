# Create and manage VMs

The scripts provision Debian cloud images with cloud-init and run QEMU/KVM
on x86_64 hosts. Both target and noise VMs support user and development modes.

Run host commands in this guide from the project's `scripts/` directory:

```bash
cd scripts
```

VM disks, seed ISOs and relative paths are resolved from that working directory.

## Quick start

    bash create_vm.sh --name=foo --type=target --ssh-pubkey=~/.ssh/id_xxxx.pub

On first run, this downloads the Debian 12 base image (~400 MB), creates a
per-VM qcow2 overlay disk, generates a cloud-init seed ISO, and boots the VM.
Subsequent runs with the same --name skip provisioning and boot straight from
the existing disk.

## Guest provisioning

For --type=target, cloud-init also installs the guest build and BPF packages:
make, gcc, gfortran, bpftool, gcc-bpf, libbpf-dev, pahole, linux-source-6.1,
libelf-dev, and zlib1g-dev. Noise VMs receive openssh-server, plus stress-ng
when launched with --mode=dev.

Both target and noise VMs also receive trace-cmd in dev mode.

Target guests then install linux-headers-$(uname -r), resolving the kernel
version inside the guest. This is a separate, best-effort installation: an
unavailable header package produces a warning in /var/log/cloud-init-output.log
without preventing the ordinary development tools from being installed.
Guest module builds still require headers matching the running kernel.

These packages are installed during first-boot provisioning only. Existing
VM disks and seed ISOs are reused; install the packages manually in an
existing guest or re-provision it as shown below. For a guest affected by the
previous unavailable-header failure, repair the tools independently:

    sudo apt-get update
    sudo apt-get install -y openssh-server make gcc gfortran \
        bpftool gcc-bpf libbpf-dev pahole linux-source-6.1 libelf-dev zlib1g-dev
    sudo systemctl enable --now ssh

Then request headers separately with sudo apt-get install "linux-headers-$(uname -r)".
If they are no longer available, install a matching kernel/header pair and
boot that kernel before building guest modules.

## Login and shutdown

Once the VM has booted (first boot takes 1-2 minutes for cloud-init):

    ssh -p 2222 debian@localhost

The default console password is "debian". To shut down cleanly from inside
the VM:

    sudo systemctl poweroff

To stop a running VM from the host:

    systemctl stop qemu-foo.scope          # graceful (ACPI shutdown)
    systemctl kill --kill-whom=all --signal=SIGKILL qemu-foo.scope  # immediate

## Arguments

Arguments use `--key=value` syntax. Run `bash create_vm.sh --help` for a summary.

```text
Required:
  --name=<name>         Name of the VM. Used for disk/ISO filenames and the
                        systemd scope. Allowed chars: letters, digits, . _ -
  --type=<type>         VM type. Must be either target or noise.
  --ssh-pubkey=<path>   Path to an SSH public key file to inject into the VM.

Optional:
  --mode=<user|dev>     VM mode (default: user). Host directory sharing is
                        enabled only in dev mode for development and testing.
  --password=<pass>     Password for the debian user (default: debian).
  --sockets=<n>         Number of CPU sockets (default: 1).
  --cores=<n>           Number of cores per socket (default: 2).
  --threads=<n>         Number of threads per core (default: 1).
                        Total vCPUs = sockets * cores * threads.
  --mem=<size>          RAM size, e.g. 4G or 512M (default: 4G).
  --disk-size=<size>    Disk image size, e.g. 20G (default: 20G).
  --base-image=<path>   Path to base qcow2 cloud image; downloaded from
                        Debian if absent (default: debian-12-generic-amd64.qcow2).
  --ssh-port=<port>     Host port forwarded to guest SSH (default: auto-select free port starting at 2222).
  --add-vsock=<bool>    Add a vhost-vsock device (default: false).
  --vsock-cid=<cid>     Guest CID for vhost-vsock. Required when
                        --add-vsock=true.
  --per-vm-cgroups=<bool>
                        Place the VM in a systemd transient scope under
                        machine.slice, matching libvirt's cgroup layout
                        (default: true). Requires systemd-run.
  --pin-to-socket=<bool>
                        Pin the VM to a specific NUMA node using numactl
                        (default: false).
  --socket-nr=<n>       NUMA node number to pin to. Required when
                        --pin-to-socket=true.

  --shared-dir=<path>  Host directory to create if absent and share read-only
                        via 9p in dev mode (default: disabled). Ignored in user
                        mode. Guest mount tag: hostshare.
  --shared-dir-readonly=<bool>
                        Enforce read-only access in QEMU (default: true).
```

## Examples

Minimal VM, 2 vCPUs, 4G RAM:

    bash create_vm.sh \
        --name=foo \
        --type=target \
        --ssh-pubkey=~/.ssh/id_ed25519.pub

36-core VM pinned to NUMA node 0 with vsock:

    bash create_vm.sh \
        --name=bench \
        --type=target \
        --ssh-pubkey=~/.ssh/id_ed25519.pub \
        --sockets=1 --cores=36 --threads=1 \
        --mem=45G \
        --pin-to-socket=true --socket-nr=0 \
        --add-vsock=true --vsock-cid=4

For development file sharing, see [Host–guest 9p sharing](host-guest-9p-sharing.md).
For a target/noise experiment with vsock tracing, see [Evaluation](evaluation.md).

## Re-provisioning

Re-provision a VM (delete its disk and seed ISO first):

    rm foo.qcow2 foo-seed.iso
    bash create_vm.sh --name=foo --type=target --ssh-pubkey=~/.ssh/id_xxxx.pub

## Host dependencies and device permissions

| Tool | Purpose | Host package examples |
| --- | --- | --- |
| `qemu-system-x86_64` | Run the VM. | Debian/Ubuntu: `qemu-system-x86`; Fedora/RHEL: `qemu-kvm`. |
| `numactl` | NUMA topology and pinning. | `numactl`. |
| `udevadm` | Target VM host-device permissions. | Debian: `udev`. |

Target VM users must belong to the host's kvm group. Host module setup installs
a udev rule granting that group read/write access to /dev/host_ivshmem and
waits for udev before launching QEMU. After adding group membership, log out
and log back in for it to take effect.

For first-boot provisioning, install `wget` to download the base image and
`xorriso` to generate the cloud-init seed ISO. The latter is packaged as
`xorriso` on Debian/Ubuntu and `libisoburn` on Arch and Gentoo.

Per-VM cgroups use `systemd-run`, supplied by systemd on most distributions.
For optional `--shared-dir` support, QEMU must support 9p and the guest kernel
must support `9p` and `9pnet_virtio`.
