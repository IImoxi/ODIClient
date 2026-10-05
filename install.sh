#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "$0")"
source_dir=$(cd -- 1.0.0/x86_64 && pwd -P)
test -f "$source_dir/libblank-client-menu.so"
mods_dir=${1:-${XDG_DATA_HOME:-$HOME/.local/share}/mcpelauncher/mods}
mkdir -p -- "$mods_dir/ODIClient/1.0.0/x86_64"
target_dir=$(cd -- "$mods_dir/ODIClient/1.0.0/x86_64" && pwd -P)
if [[ "$source_dir" != "$target_dir" ]]; then
    install -m 644 -- "$source_dir/libblank-client-menu.so" "$source_dir/mod.json" "$target_dir/"
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
echo "Installed ODIClient in $target_dir."
echo 'Restart the launcher, open Mods > Installed Mods > ODIClient, and click Activate.'
