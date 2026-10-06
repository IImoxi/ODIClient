#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "$0")"
mkdir -p build

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
host_sources=(autosprint motion_blur custom_menu menu_pages custom_font analog_input auto_gg chat fps_limiter fps_display display_layout zoom hook_manager render render_frame_trace render_gl_trace gpu_multidraw gpu_uniform_cache tablist skin_image flarial_presence particles experimental popup environment)
host_objects=()
for source in "${host_sources[@]}"; do
    extra_flags=()
    case "$source" in
        custom_font) extra_flags=(-I/usr/include/freetype2) ;;
        auto_gg|chat|fps_limiter|fps_display|display_layout|zoom|hook_manager|render|render_frame_trace|render_gl_trace|gpu_multidraw|gpu_uniform_cache|tablist|skin_image|flarial_presence|particles|experimental|popup|environment) extra_flags=(-ffreestanding) ;;
    esac
    clang++ "${host_flags[@]}" "${extra_flags[@]}" -c "$source.cpp" -o "build/$source.o"
    host_objects+=("build/$source.o")
done
clang++ "${flags[@]}" -fvisibility=hidden client.cpp runtime.cpp -Lbuild \
    "${host_objects[@]}" -Wl,--no-as-needed -lmcpelauncher_gamewindow -lmcpelauncher_menu -lmcpelauncher_mod \
    -Wl,--no-undefined -Wl,-soname,libblank-client-menu.so \
    -o build/libblank-client-menu.so
mkdir -p 1.0.0/x86_64
install -m 644 build/libblank-client-menu.so 1.0.0/x86_64/libblank-client-menu.so
install -D -m 644 assets/inter.ttf 1.0.0/x86_64/assets/inter.ttf
install -m 644 assets/icon-*.png 1.0.0/x86_64/assets/
bash install.sh "${1:-${XDG_DATA_HOME:-$HOME/.local/share}/mcpelauncher/mods}"
