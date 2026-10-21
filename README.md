# Phantom Tracker

<p align="center">
  <img src="Documentation/Images/phantom_vcpu.png" alt="Phantom vCPU" width="600">
</p>

Oversubscription is a popular cost-cutting practice in cloud deployments, which leads to the phenomenon of untimely vCPU preemptions. As presented in this thesis [1], a phantom vCPU is such a preempted vCPU which is currently waiting in the runqueue of a pCPU on the host, and as a consequence, the thread running on it inside the guest is now stalled. Phantom Tracker is an algorithmic solution that leverages eBPF and paravirtualized task scheduling to detect and quantify such phantom vCPUs accurately.

<p align="center">
  <img src="Documentation/Images/phantom_tracker_prototype.png" alt="Prototype" width="450">
</p>

This implementation is part of a GSoC Project [2] that targets QEMU/KVM virtual machines, uses ivshmem-plain device [3] for facilitating the paravirtualized task scheduling related communication, and provides integration tools for using Phantom Tracker with libgomp, i.e. GCC's implementation of OpenMP.

Contributor instructions for building guest-compatible libgomp on the host
are in [dev-tools/libgomp](dev-tools/libgomp/README). This development tool is
independent of VM provisioning and is not required for packaged installations.

For the scheduler noise workload used in development experiments,
[dev-tools/stress-ng](dev-tools/stress-ng/README) builds a pinned stress-ng
binary on the host for distribution to multiple noise VMs.

## Usage guides

- [VM creation and provisioning](Documentation/vm-creation.md)
- [Host–guest development sharing](Documentation/host-guest-sharing.md)
- [Script helper libraries](Documentation/script-libraries.md)

## References
[1] https://inria.hal.science/tel-05438117v2

[2] https://gcc.gnu.org/wiki/SummerOfCode#OMP_DYNAMIC_POLICY

[3] https://www.qemu.org/docs/master/system/devices/ivshmem.html
