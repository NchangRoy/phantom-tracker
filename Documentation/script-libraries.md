# Script helper libraries

Entrypoints live in `scripts/`; reusable and internal helpers live in
`scripts/lib/`. All named options use `--key=value` syntax.

| Library | Purpose |
| --- | --- |
| [args.sh](../scripts/lib/args.sh) | Named arguments through `declare_arg`, `parse_args`, and `print_usage`. |
| [cloudinit.sh](../scripts/lib/cloudinit.sh) | Download the Debian image, create qcow2 overlays, and generate cloud-init seed ISOs using xorriso. |
| [distro.sh](../scripts/lib/distro.sh) | Detect the host distro and print package-manager install hints through `check_cmd`. |
| [evaluate.sh](../scripts/lib/evaluate.sh) | Host-side SSH, noise configuration, trace-agent, governor and cleanup helpers. |
| [evaluate_batch.sh](../scripts/lib/evaluate_batch.sh) | Run the continuous noise process and target benchmark batch inside the trace recording. |
| [evaluate_guest.sh](../scripts/lib/evaluate_guest.sh) | Validate and run the guest-local benchmark and record its output and runtimes. |
| [numa.sh](../scripts/lib/numa.sh) | Read NUMA topology from sysfs through `numa_check`, `numa_node_count`, and `numa_cpulist`. |
| [ssh.sh](../scripts/lib/ssh.sh) | Guest SSH readiness and best-effort development-share mounting. |

For contributor ideas, see [CONTRIBUTING-IDEAS](../scripts/lib/CONTRIBUTING-IDEAS).
