#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Contributors:
#   Human: Himadri Chhaya-Shailesh
#   AI: Claude Sonet 4.6
#
# NUMA topology helpers.
#
# Provides:
#   numa_check              — verify NUMA sysfs is available
#   numa_node_count         — print the number of NUMA nodes
#   numa_cpulist <node-nr>  — print the cpulist for the given NUMA node
#                             (e.g. "0-11,24-35")

# Sysfs NUMA node directory
_NUMA_SYS="/sys/devices/system/node"

# Verify that NUMA sysfs is present.
numa_check() {
    if [[ ! -d "$_NUMA_SYS" ]]; then
        echo "error: NUMA sysfs not found at $_NUMA_SYS" >&2
        return 1
    fi
}

# Print the number of NUMA nodes on the host.
numa_node_count() {
    numa_check || return 1
    local count
    count=$(find "$_NUMA_SYS" -maxdepth 1 -name 'node[0-9]*' -type d | wc -l)
    echo "$count"
}

# Print the cpulist for the given NUMA node number.
# The cpulist format follows the Linux kernel range syntax, e.g. "0-11,24-35".
#
# Usage: numa_cpulist <node-nr>
numa_cpulist() {
    numa_check || return 1

    local node_nr="$1"
    local cpulist_file="$_NUMA_SYS/node${node_nr}/cpulist"

    if [[ ! -f "$cpulist_file" ]]; then
        echo "error: NUMA node $node_nr not found (checked $cpulist_file)" >&2
        return 1
    fi

    cat "$cpulist_file"
}
