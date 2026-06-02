#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Contributors:
#   Human: Nchang Roy Fru
#   AI: Chatgpt GPT-5.3-mini
#
# VM registration helper script.
#
# This script:
#   - Parses VM socket and VM name from CLI arguments
#   - Starts the eBPF loader in the background
#   - Waits briefly for initialization
#   - Prepares VM registration flow (QMP + vCPU tracking expected)
#
# Expected usage:
#   ./script.sh --socket=<vm_socket> --name=<vm_name>

# Parse VM socket argument
# Expected format: --socket=<vm_socket>
if [[ "$1" == --socket=* ]]; then
    VM_SOCKET="${1#--socket=}"
    echo "socket:  $VM_SOCKET"
else
    echo "Invalid argument: $1"
    echo "Expected: --socket=<vm_socket>"
    exit 1
fi

# Parse VM name argument
# Expected format: --name=<vm_name>
if [[ "$2" == --name=* ]]; then
    VM_NAME="${2#--name=}"
    echo "vm_name: $VM_NAME"
else
    echo "Invalid argument: $2"
    echo "Expected: --name=<vm_name>"
    exit 1
fi


# ------------------------------------------------------------
# Initialization phase
# ------------------------------------------------------------

echo "Creating maps..."

echo "Connecting to vm socket $VM_SOCKET"

# Start the eBPF loader in the background
# This is expected to:
#   - load BPF program into kernel
#   - create/register required maps
#   - initialize tracking structures
 sudo ./../pvsched-host/bin/create_maps.loader 
# Optional: give loader time to initialize maps and attach probes
sleep 1

# Start the ebpf timer in the background
# This is expected to:
#   - load a timer program for periodically calculating the phantom average

 sudo ./../pvsched-host/bin/timer.loader 

sleep 1

#ping lo interface to start timer

ping -c 2 localhost




# ------------------------------------------------------------
# VM registration phase
# ------------------------------------------------------------

echo -e "\nRegistering VM $VM_NAME..."

# Run VM registration program:
#   - communicates with QEMU via QMP socket
#   - queries VM configuration
#   - registers vCPUs into eBPF maps
#
# NOTE: currently commented out (enable when ready)
 sudo ./../pvsched-host/bin/register_vm "$VM_SOCKET" "$VM_NAME" &