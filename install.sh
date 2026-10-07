#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "$0")"
source_dir=$(cd -- 1.0.0/x86_64 && pwd -P)
test -f "$source_dir/libblank-client-menu.so"
if [[ ! -f "$source_dir/build-info.txt" ]]; then
    echo 'Build identity missing; run bash build.sh first.' >&2
    exit 1
fi
expected_sha=$(sed -n 's/^binary_sha256=//p' "$source_dir/build-info.txt")
source_sha=$(sha256sum "$source_dir/libblank-client-menu.so")
if [[ -z "$expected_sha" || "${source_sha%% *}" != "$expected_sha" ]]; then
    echo 'Package binary does not match build-info.txt; rebuild before installing.' >&2
    exit 1
fi
mods_dir=${1:-${XDG_DATA_HOME:-$HOME/.local/share}/mcpelauncher/mods}
mkdir -p -- "$mods_dir/ODIClient/1.0.0/x86_64"
target_dir=$(cd -- "$mods_dir/ODIClient/1.0.0/x86_64" && pwd -P)
if [[ "$source_dir" != "$target_dir" ]]; then
    install -m 644 -- "$source_dir/libblank-client-menu.so" "$source_dir/mod.json" "$source_dir/build-info.txt" "$target_dir/"
fi
install -m 644 -- nuphy_analog.py nuphy_distance.py README.md "$target_dir/"
mkdir -p -- "$target_dir/assets"
install -m 644 -- assets/inter.ttf "$target_dir/assets/"
install -m 644 -- assets/icon-*.png "$target_dir/assets/"
# Migrate the symlink created by the earlier installer to profile activation.
legacy_link=$mods_dir/libblank-client-menu.so
if [[ -L "$legacy_link" && $(readlink -- "$legacy_link") == blank-client-menu/libblank-client-menu.so ]]; then
    unlink -- "$legacy_link"
fi
cmp -- "$source_dir/libblank-client-menu.so" "$target_dir/libblank-client-menu.so"
installed_sha=$(sha256sum "$target_dir/libblank-client-menu.so")
[[ "${installed_sha%% *}" == "$expected_sha" ]]
source_id=$(sed -n 's/^source_id=//p' "$target_dir/build-info.txt")
echo "Installed ODIClient in $target_dir."
echo "Verified build ${source_id:0:12}; binary SHA-256: $expected_sha"
echo 'Restart the launcher, open Mods > Installed Mods > ODIClient, and click Activate.'
