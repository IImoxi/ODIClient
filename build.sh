#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "$0")"
mkdir -p build
# Fingerprint actual inputs (including local edits), not the unchanged package version.
source_fingerprint() {
    { sha256sum -- *.cpp *.h *.py build.sh install.sh assets/* 1.0.0/x86_64/mod.json; clang++ --version; } | sha256sum
}
source_id=$(source_fingerprint)
source_id=${source_id%% *}
echo "Source: $(pwd -P)"
echo "Build ID: ${source_id:0:12}"

# No libc/C++ runtime is linked into the mod.
flags=(--target=x86_64-linux-android21 -fuse-ld=bfd -std=c++17 -O2 -ffreestanding
       -fPIC -shared -nostdlib -fno-exceptions -fno-rtti -fno-stack-protector
       -Wl,--hash-style=both -Wl,-z,noexecstack -Wall -Wextra -Werror)
clang++ "${flags[@]}" -DWINDOW_API api_stubs.cpp \
    -Wl,-soname,libmcpelauncher_gamewindow.so -o build/libmcpelauncher_gamewindow.so
clang++ "${flags[@]}" api_stubs.cpp \
    -Wl,-soname,libmcpelauncher_menu.so -o build/libmcpelauncher_menu.so
clang++ "${flags[@]}" -DHOST_API api_stubs.cpp \
    -Wl,-soname,libmcpelauncher_mod.so -o build/libmcpelauncher_mod.so
# Host adapters resolve Linux APIs dynamically; the final mod stays Android x86_64.
host_flags=(-std=c++17 -O2 -fPIC -fvisibility=hidden -fno-exceptions -fno-rtti
            -fno-stack-protector -Wall -Wextra -Werror)
host_sources=(autosprint motion_blur custom_menu menu_pages custom_font analog_input auto_gg chat fps_limiter fps_display display_layout zoom hook_manager render render_frame_trace render_gl_trace gpu_shader_services tablist skin_image flarial_presence particles popup environment player_target sky_renderer projection_jitter)
host_objects=()
for source in "${host_sources[@]}"; do
    extra_flags=()
    case "$source" in
        custom_font) extra_flags=(-I/usr/include/freetype2) ;;
        auto_gg|chat|fps_limiter|fps_display|display_layout|zoom|hook_manager|render|render_frame_trace|render_gl_trace|gpu_shader_services|tablist|skin_image|flarial_presence|particles|popup|environment|player_target|sky_renderer|projection_jitter) extra_flags=(-ffreestanding) ;;
    esac
    clang++ "${host_flags[@]}" "${extra_flags[@]}" -c "$source.cpp" -o "build/$source.o"
    host_objects+=("build/$source.o")
done
clang++ "${flags[@]}" -fvisibility=hidden client.cpp runtime.cpp -Lbuild \
    "${host_objects[@]}" -Wl,--no-as-needed -lmcpelauncher_gamewindow -lmcpelauncher_menu -lmcpelauncher_mod \
    -Wl,--no-undefined -Wl,-soname,libblank-client-menu.so \
    -o build/libblank-client-menu.so
current_id=$(source_fingerprint)
if [[ "${current_id%% *}" != "$source_id" ]]; then
    echo 'Sources changed during compilation; rebuild before installing.' >&2
    exit 1
fi
mkdir -p 1.0.0/x86_64
install -m 644 build/libblank-client-menu.so 1.0.0/x86_64/libblank-client-menu.so
install -D -m 644 assets/inter.ttf 1.0.0/x86_64/assets/inter.ttf
install -m 644 assets/icon-*.png 1.0.0/x86_64/assets/
binary_sha=$(sha256sum build/libblank-client-menu.so)
printf 'source_id=%s\nbinary_sha256=%s\n' "$source_id" "${binary_sha%% *}" > build/build-info.txt
install -m 644 build/build-info.txt 1.0.0/x86_64/build-info.txt
bash install.sh "${1:-${XDG_DATA_HOME:-$HOME/.local/share}/mcpelauncher/mods}"
