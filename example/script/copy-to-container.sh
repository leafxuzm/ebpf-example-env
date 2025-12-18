#!/usr/bin/env bash
set -euo pipefail
usage() { echo "Usage: $0 <src_path> <container_id> [dest_path=/workspace/projects/]"; exit 1; }
src="${1:-}"; cid="${2:-}"; dest="${3:-/workspace/projects/}"
if [ -z "$src" ] || [ -z "$cid" ]; then usage; fi
if [ ! -e "$src" ]; then echo "Source not found: $src"; exit 2; fi
if ! command -v docker >/dev/null 2>&1; then echo "docker not found"; exit 3; fi
if ! docker container inspect "$cid" >/dev/null 2>&1; then echo "Container not found: $cid"; exit 4; fi
docker exec "$cid" mkdir -p "$dest"
docker cp "$src" "$cid":"$dest"
name="$(basename "$src")"
echo "Copied $src to $cid:$dest$name"
