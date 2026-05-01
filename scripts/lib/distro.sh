#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Contributors:
#   Human: Himadri Chhaya-Shailesh
#   AI: Claude Sonet 4.6
#
# Distro detection and package install hint helpers.
#
# Provides:
#   distro_id                        — print the distro ID
#   distro_install_hint <pkg> [...]  — print a distro-specific install command
#   check_cmd <cmd> <pkg> [...]      — error with install hint if cmd is missing

# Print the distro ID from /etc/os-release (e.g. "debian", "ubuntu", "fedora").
distro_id() {
    if [[ -f /etc/os-release ]]; then
        # shellcheck source=/dev/null
        . /etc/os-release
        echo "${ID:-unknown}"
    else
        echo "unknown"
    fi
}

# Print a distro-specific install command for the given package.
# The package name is assumed to be the same across distros unless
# overridden via KEY=VALUE pairs after the package name.
#
# Usage: distro_install_hint <default-pkg> [distro=pkg ...]
# Example:
#   distro_install_hint xorriso arch=libisoburn gentoo=libisoburn
distro_install_hint() {
    local default_pkg="$1"
    shift

    local distro
    distro="$(distro_id)"

    # Build override map from KEY=VALUE args
    local pkg="$default_pkg"
    for override in "$@"; do
        local key="${override%%=*}"
        local val="${override#*=}"
        if [[ "$distro" == "$key" ]]; then
            pkg="$val"
            break
        fi
    done

    case "$distro" in
        debian|ubuntu|linuxmint|pop)
            echo "  apt install $pkg" ;;
        fedora)
            echo "  dnf install $pkg" ;;
        rhel|centos|almalinux|rocky)
            echo "  dnf install $pkg" ;;
        opensuse*|sles)
            echo "  zypper install $pkg" ;;
        arch|manjaro|endeavouros)
            echo "  pacman -S $pkg" ;;
        alpine)
            echo "  apk add $pkg" ;;
        gentoo)
            echo "  emerge $pkg" ;;
        nixos)
            echo "  nix-env -iA nixpkgs.$pkg" ;;
        *)
            echo "  (unknown distro '$distro': install '$pkg' with your package manager)" ;;
    esac
}

# Check that a command is available; if not, print an error with a distro-
# specific install hint and return 1.
#
# Usage: check_cmd <cmd> <default-pkg> [distro=pkg ...]
# Example:
#   check_cmd numactl numactl
#   check_cmd xorriso xorriso arch=libisoburn gentoo=libisoburn
check_cmd() {
    local cmd="$1"
    shift
    if ! command -v "$cmd" &>/dev/null; then
        echo "error: required tool not found: $cmd" >&2
        echo "  install it with:" >&2
        distro_install_hint "$@" >&2
        return 1
    fi
    return 0
}
