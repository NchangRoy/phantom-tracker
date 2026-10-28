# Host–guest 9p sharing for development

Host–guest 9p sharing exposes a host directory as a guest filesystem for
project development and testing. It is separate from the ivshmem communication
used by the pvsched components. Sharing is optional in dev mode for both VM
types and is disabled in user mode. See [VM creation](vm-creation.md) for
provisioning and other launcher options.

## Launch with a 9p share

Run this host command from the project's `scripts/` directory, for either
new or existing VM disks:

    bash create_vm.sh \
        --name=foo \
        --type=target \
        --ssh-pubkey=~/.ssh/id_ed25519.pub \
        --mode=dev --shared-dir=./shared

## Access and automatic mounting

The 9p shared directory supports development and testing of Phantom Tracker,
including making host sources and build artifacts available in the guest.
The default user mode does not attach a 9p shared directory, even if
--shared-dir is supplied. In dev mode, the script creates ./shared on the
host if needed. For target VMs, it waits for SSH and mounts the share at
/mnt/hostshare automatically, including when reusing an existing guest disk.
The mount uses ro by default, or rw with --shared-dir-readonly=false.

Automatic mounting is best effort. It requires --ssh-pubkey to end in .pub
and an unencrypted matching private key alongside it. Only that identity is
used, with no fallback to other keys, agents, or passwords. The guest must
authorize that key for debian and permit passwordless sudo (configured by
cloud-init). Host-key trust must already be established manually, for example
by connecting with ssh target once; no host keys are accepted automatically.
Missing/mismatched keys, authentication failures, and mount errors produce
a warning without stopping the VM. Only connection readiness errors are
retried. Existing mounts are left untouched.

## Manual mounting

Noise VMs require manual mounting. These commands also provide a fallback
if automatic mounting fails:

    sudo mkdir -p /mnt/hostshare
    sudo mount -t 9p -o trans=virtio,version=9p2000.L,ro hostshare /mnt/hostshare

## Guest reboots

After the first successful automatic mount, the guest's /etc/fstab is backed
up to /etc/fstab.phantom-tracker.bak and an entry is appended if neither the
share nor its mount point already has one. Existing entries are preserved.
For the default read-only export, the generated entry is:

    hostshare /mnt/hostshare 9p trans=virtio,version=9p2000.L,ro,nofail,x-systemd.mount-timeout=15s 0 0

On subsequent guest boots, systemd attempts the mount once with a 15-second
timeout. Failure does not block boot, including when the VM is started
without a share in user mode. No SSH connection is needed for these later
attempts. If the boot-time mount failed, the launcher does not retry it in
the same boot. Diagnose it inside the guest with:

    journalctl -b -u mnt-hostshare.mount

If initial SSH authentication or mounting fails, no fstab entry is added.
When changing ro/rw for a previously configured guest, update its existing
fstab entry manually; automatic setup leaves that entry untouched.

## Read-only access and symlinks

Start the VM with --mode=dev and the same --shared-dir each time. QEMU enforces
read-only access, even if the guest tries to remount read-write. To allow writes,
use --shared-dir-readonly=false and mount with rw instead of ro. Host access
uses the account running QEMU. Omitting --shared-dir disables host–guest 9p sharing.

Host–guest 9p sharing uses security_model=none to expose ordinary host files and symlinks,
including build trees copied into the share on the host. Keep copied symlink
targets within the shared tree, using relative links where possible. Absolute
host paths resolve in the guest's filesystem. In case the need of changing the
9p sharing mode, shut down and relaunch QEMU, then remount the share. A guest-only
reboot does not change the QEMU export settings.
