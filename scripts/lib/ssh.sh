#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Contributors:
#   Human: Himadri Chhaya-Shailesh
#   AI: Claude Sonet 4.6, ChatGPT-5.5
#
# SSH networking helpers.

# Mount a target dev VM's share once SSH is ready. This also works with
# existing guest disks, where cloud-init's first-boot setup has already run.
# Usage: ssh_mount_hostshare <port> <public-key-path> <ro|rw>
ssh_mount_hostshare() {
    local port="$1" pubkey="$2" access="$3"
    local attempt output="" private_key derived_key expected_key rc
    if ! command -v ssh >/dev/null || ! command -v ssh-keygen >/dev/null; then
        echo "warning: automatic share mount skipped: ssh and ssh-keygen are required" >&2
        return 1
    fi
    if [[ "$pubkey" != *.pub || ! -r "$pubkey" || ! -r "${pubkey%.pub}" ]]; then
        echo "warning: automatic share mount skipped: expected $pubkey and its matching private key ${pubkey%.pub}" >&2
        return 1
    fi
    private_key=${pubkey%.pub}
    if ! derived_key=$(ssh-keygen -y -P '' -f "$private_key" 2>&1); then
        echo "warning: automatic share mount skipped: cannot read private key $private_key non-interactively: $derived_key" >&2
        return 1
    fi
    expected_key=$(awk 'NR == 1 {print $1, $2}' "$pubkey")
    derived_key=$(awk 'NR == 1 {print $1, $2}' <<< "$derived_key")
    if [[ "$derived_key" != "$expected_key" ]]; then
        echo "warning: automatic share mount skipped: $private_key does not match $pubkey" >&2
        return 1
    fi
    local -a ssh_args=(
        -F /dev/null -i "$private_key" -o IdentitiesOnly=yes -o IdentityAgent=none
        -o BatchMode=yes -o ConnectTimeout=5 -o ConnectionAttempts=1
        -o StrictHostKeyChecking=yes -o PreferredAuthentications=publickey
        -o ServerAliveInterval=5 -o ServerAliveCountMax=1
        -p "$port"
    )

    for ((attempt=0; attempt<60; attempt++)); do
        if output=$(ssh "${ssh_args[@]}" debian@localhost "sudo -n sh -s -- $access" 2>&1 <<'EOF'
set -eu
access=$1
mkdir -p /mnt/hostshare
if mountpoint -q /mnt/hostshare; then
    if [ "$(findmnt -n -o SOURCE --target /mnt/hostshare)" != hostshare ] ||
       [ "$(findmnt -n -o FSTYPE --target /mnt/hostshare)" != 9p ]; then
        echo "error: /mnt/hostshare is occupied by another filesystem" >&2
        exit 1
    fi
    # An existing mount is left untouched.
    :
else
    if systemctl is-failed --quiet mnt-hostshare.mount; then
        echo "error: the boot-time share mount already failed; not retrying this boot" >&2
        journalctl -b -u mnt-hostshare.mount -n 10 --no-pager >&2
        exit 1
    fi
    timeout 15 mount -t 9p -o "trans=virtio,version=9p2000.L,$access" hostshare /mnt/hostshare
fi
# Persist only after a successful mount. Do not replace an administrator's
# existing entry for this share or mount point.
if ! awk '$1 == "hostshare" || $2 == "/mnt/hostshare" { found=1 } END { exit !found }' /etc/fstab; then
    if [ ! -e /etc/fstab.phantom-tracker.bak ]; then
        cp -p /etc/fstab /etc/fstab.phantom-tracker.bak
    fi
    printf '\nhostshare /mnt/hostshare 9p trans=virtio,version=9p2000.L,%s,nofail,x-systemd.mount-timeout=15s 0 0\n' "$access" >> /etc/fstab
    systemctl daemon-reload
    echo "shared-dir: configured best-effort mounting on guest boot"
fi
findmnt -n -o OPTIONS --target /mnt/hostshare
EOF
        ); then
            echo "shared-dir: available at /mnt/hostshare in the guest ($output)"
            return 0
        else
            rc=$?
        fi
        # Retry boot/readiness failures only. Authentication, host-key trust,
        # and guest mount errors require user action rather than more attempts.
        if [[ "$rc" -ne 255 ]]; then break; fi
        case "$output" in
            *"Connection refused"*|*"Connection timed out"*|*"Connection reset"*|*"Connection closed"*) ;;
            *) break ;;
        esac
        sleep 2
    done
    echo "warning: automatic share mount failed for debian@localhost:$port using $private_key: $output" >&2
    echo "  verify SSH access and host-key trust manually; the VM will remain running" >&2
    return 1
}

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
