#!/usr/bin/env bash
set -euo pipefail

client_dir=$(cd -- "$(dirname -- "$0")" && pwd -P)
mkdir -p -- "$client_dir/Logs"
export ODI_TERRAIN_TRACE="$client_dir/Logs/terrain-v6.csv"

# A running Qt launcher may receive the launch request without the new environment.
if pgrep -u "$UID" -f '(^|/)mcpelauncher-ui-qt([[:space:]]|$)' >/dev/null; then
    echo 'Close the existing Minecraft launcher completely, then run this script again.' >&2
    exit 1
fi

printf 'Terrain trace destination: %s\n' "$ODI_TERRAIN_TRACE"
echo 'The mod creates this file when Minecraft starts with ODIClient active.'
exec mcpelauncher-ui-qt "$@"
