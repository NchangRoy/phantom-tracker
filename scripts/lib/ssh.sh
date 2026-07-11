#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Contributors:
#   Human: Himadri Chhaya-Shailesh
#   AI: Claude Sonet 4.6, ChatGPT-5.5
#
# SSH networking helpers.

# Return success when a local TCP listening socket is using the given port.
tcp_port_is_in_use() {
    local port="$1"

    # Prefer ss to detect whether any local TCP listener is bound to this port.
    if command -v ss &>/dev/null; then
        ss -H -ltn "sport = :$port" | grep -q .
        return $?
    fi

    # Fallback for systems without ss: inspect netstat listening sockets.
    if command -v netstat &>/dev/null; then
        netstat -ltn | awk '{print $4}' | grep -Eq "[:.]${port}$"
        return $?
    fi

    # Last fallback: read kernel TCP tables and match the hex-encoded local port.
    if [[ -r /proc/net/tcp || -r /proc/net/tcp6 ]]; then
        local hexport
        hexport="$(printf '%04X' "$port")"
        awk 'NR > 1 { print $2 }' /proc/net/tcp /proc/net/tcp6 2>/dev/null | grep -Eiq ":${hexport}$"
        return $?
    fi

    return 1
}

# Echo the first free local TCP port from a given starting point.
find_free_tcp_port() {
    local port="$1"

    while (( port <= 65535 )); do
        if ! tcp_port_is_in_use "$port"; then
            echo "$port"
            return 0
        fi
        ((port++))
    done

    return 1
}
