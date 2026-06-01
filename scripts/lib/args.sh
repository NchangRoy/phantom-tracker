#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Contributors:
#   Human: Himadri Chhaya-Shailesh
#   AI: Claude Sonet 4.6
#
# Reusable named-argument parsing library.
#
# Usage in a script:
#   . "$(dirname "$0")/lib/args.sh"
#
#   declare_arg name     required "Name of the VM"
#   declare_arg dry-run  optional "Print commands without executing"
#   declare_arg cgroups  optional "Use cgroups" true
#
#   parse_args "$@"
#
# After parse_args, each argument is available as ARG_<NAME> (uppercased,
# hyphens replaced by underscores). E.g.:
#   --name    -> ARG_NAME
#   --dry-run -> ARG_DRY_RUN
#
# Optional arguments with a default are pre-set before parsing; the user
# can override them on the command line.

_ARG_NAMES=()
_ARG_REQUIRED=()
_ARG_DESCS=()
_ARG_DEFAULTS=()

# declare_arg <name> <required|optional> <description> [default]
declare_arg() {
    local name="$1"
    local req="$2"
    local desc="$3"
    local default="${4:-}"

    _ARG_NAMES+=("$name")
    _ARG_REQUIRED+=("$req")
    _ARG_DESCS+=("$desc")
    _ARG_DEFAULTS+=("$default")
}

print_usage() {
    local script="${BASH_SOURCE[-1]:-$0}"
    echo "usage: $(basename "$script") [options]" >&2
    echo >&2
    echo "options:" >&2
    for i in "${!_ARG_NAMES[@]}"; do
        local name="${_ARG_NAMES[$i]}"
        local req="${_ARG_REQUIRED[$i]}"
        local desc="${_ARG_DESCS[$i]}"
        local default="${_ARG_DEFAULTS[$i]:-}"
        local extra="$req"
        [[ -n "$default" ]] && extra="$req, default: $default"
        printf "  --%-25s %s (%s)\n" "$name=<value>" "$desc" "$extra" >&2
    done
}

parse_args() {
    # Apply defaults before parsing
    for i in "${!_ARG_NAMES[@]}" ; do
        local default="${_ARG_DEFAULTS[$i]:-}"
        if [[ -n "$default" ]]; then
            local var="ARG_${_ARG_NAMES[$i]//-/_}"
            var="${var^^}"
            printf -v "$var" '%s' "$default"
        fi
    done

    while [[ $# -gt 0 ]]; do
        case "$1" in
            --help|-h)
                print_usage
                exit 0
                ;;
            --*)
                if [[ "$1" != *=* ]]; then
                    echo "error: expected --${1#--}=<value>" >&2
                    print_usage
                    exit 1
                fi
                local key="${1#--}"
                local value="${key#*=}"
                key="${key%%=*}"
                local var="ARG_${key//-/_}"
                var="${var^^}"
                printf -v "$var" '%s' "$value"
                shift
                ;;
            *)
                echo "error: unknown argument: $1" >&2
                print_usage
                exit 1
                ;;
        esac
    done

    # Check required args
    local missing=0
    for i in "${!_ARG_NAMES[@]}"; do
        if [[ "${_ARG_REQUIRED[$i]}" == "required" ]]; then
            local name="${_ARG_NAMES[$i]}"
            local var="ARG_${name//-/_}"
            var="${var^^}"
            if [[ -z "${!var:-}" ]]; then
                echo "error: missing required argument --$name" >&2
                missing=1
            fi
        fi
    done

    if [[ "$missing" -ne 0 ]]; then
        print_usage
        exit 1
    fi
}
