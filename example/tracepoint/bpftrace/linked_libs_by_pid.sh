#!/usr/bin/env bash
set -euo pipefail
if [ $# -lt 1 ]; then
  echo "usage: $0 <pid>"
  exit 1
fi
pid="$1"
maps="/proc/$pid/maps"
status="/proc/$pid/status"
comm_file="/proc/$pid/comm"
if [ ! -r "$maps" ]; then
  echo "error: cannot read $maps"
  exit 2
fi
if [ ! -r "$status" ]; then
  echo "error: cannot read $status"
  exit 3
fi
comm="$(cat "$comm_file" 2>/dev/null || echo unknown)"
uid="$(awk '/^Uid:/ {print $2}' "$status")"
user="$(id -nu "$uid" 2>/dev/null || getent passwd "$uid" | cut -d: -f1 || echo unknown)"
echo "baseline for pid=$pid comm=$comm uid=$uid user=$user"
awk '/\\.so(\\.|$)/ && $NF ~ /^\// {print $NF}' "$maps" | sort -u | sed 's/^/  lib: /'
if command -v bpftrace >/dev/null 2>&1; then
  script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
  if [ "${EUID:-$(id -u)}" -eq 0 ]; then
    bpftrace "$script_dir/linked_libs_by_pid.bt" "$pid"
  elif command -v sudo >/dev/null 2>&1; then
    sudo bpftrace "$script_dir/linked_libs_by_pid.bt" "$pid"
  else
    echo "insufficient privileges to run bpftrace"
  fi
else
  echo "bpftrace not found; skipping live monitoring"
fi
