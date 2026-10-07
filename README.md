# ODIClient

A feature mod for MCPELauncher on Linux. It adds a custom in-game menu and optional gameplay, chat, rendering, and roster features.

## Compatibility

- MCPELauncher 1.8.4 on x86_64 with SDL3/Wayland or EGLUT/X11 (including XWayland).
- Native Minecraft hooks support **Minecraft 1.26.52.3 Android x86_64** only. Hook-based features stay unavailable on other game builds.
- Arch Linux is the tested build environment. Other distributions and launcher versions are unverified.

## Install

Install MCPELauncher and your own Minecraft game files first. From the ODIClient source folder, install build dependencies and build:

```sh
sudo pacman -S clang binutils libx11 libglvnd freetype2 curl json-c
bash build.sh
```

The build compiles the mod and installs it to the default MCPELauncher mods folder. To use a different mods directory, pass it as an argument:

```sh
bash build.sh /path/to/mods
```

The build prints its source folder, install folder, build ID and binary SHA-256.
`build/build-info.txt` and `1.0.0/x86_64/build-info.txt` record the identity;
the installer rejects a mismatched package and verifies the installed bytes.
Compare the recorded identity with the terminal output when verifying an installation.
The package version stays 1.0.0 and is not a build identifier. Both you and Codex use `bash build.sh`
in this source folder.

Restart the launcher, open **Mods → Installed Mods → ODIClient**, and activate it for your profile. Launch Minecraft and press **L** to open the menu. Use the same launcher page to disable it.

A prebuilt release archive can be installed without a compiler by extracting it and running `bash install.sh` from its folder.

## Features

Open the menu with **L**. On a module tile, left-click toggles it; right-click opens its settings. Press **Escape** to return to the tiles, then press it again to close the menu. Settings use a shared glass-style switch with a left label and a sliding square X/check thumb; switching animates in both directions. Click the label or switch to toggle it. Master toggles have no enclosing bubble; faint 1-pixel white lines separate settings groups, consecutive description rows stay together, and child settings disappear when their master is off. Settings are saved beside the installed mod.

| Feature | What it does |
| --- | --- |
| **Zoom** | Hold **C** and scroll to zoom from 1.5× to 30×. Change the key and scroll step in settings. Defaults off. |
| **Tablist** | Hold **Tab** to view the roster. Scroll to select a player and preview their skin; hold right-click for one second to save the skin as a PNG. Defaults on. |
| **Auto Sprint** | Simulates Left Ctrl while moving forward, using Minecraft’s default bindings. |
| **Particles** | Shows local critical-hit particles on player attack attempts. Cosmetic only; does not change damage. Defaults off. |
| **Motion Blur** | Blends recent frames or averages over a selected frame-time window. Requires EGL with OpenGL ES 3 or desktop OpenGL 3.3. |
| **Screen Blur** | Experimental full-screen spatial blur, including HUD and menus. The ODIClient menu remains sharp. Defaults off. |
| **FPS Display** | Average FPS with an optional 1% low alongside it. Average/update interval: 250–2000 ms in 250 ms steps (default 1000 ms). Font scale uses the shared 0.5×–6× tiers (default 1×). Select a corner using the four dots in the 16:9 Anchor control; displays sharing a corner stack vertically toward the screen center. Defaults off; settings persist. Samples focused rendering, including menus; focus loss resets the window. |
| **FPS Limit** | Caps rendering from 30 to 480 FPS. Minecraft’s FPS setting or VSync may impose a lower cap. |
| **Environment** | Optional Time changer with a 0–23999 tick slider (6000 = noon, 18000 = midnight), Fog color with Hue (0–360°), Saturation and Value (0–100%) sliders, and a saved Physically inspired sky switch. It replaces the sky with a full-screen atmosphere, procedural sun, moon and stars. Its status distinguishes compiled shaders from a linked program and an observed replacement draw. Physically inspired sky defaults OFF, Vanilla sun/moon OFF. GPU atmosphere caching, half-resolution atmosphere and reduced integration samples (6×3) are always enabled. The cache is reused across tiny sky-time changes; sun, moon and stars retain full resolution. These optimizations can slightly soften the horizon or shift gradients/colors. Unsupported floating-point texture paths fall back to direct atmosphere rendering with reduced samples. All environment settings persist. In-game routing, camera alignment and performance still need verification. Changes are local; server time stays unchanged. |
| **Render** | Optional horizontal and vertical terrain distance limits. These can hide visible cliffs or cave openings. |
| **AutoGG** | Sends a configurable response to matching incoming chat. Defaults to matching “You won the game” and sending “gg”. |
| **Chat mods** | Optionally hides incoming messages that match a comma-separated keyword list. |
| **Lobby Scanner** | Runs configured commands or sends messages when matching players join. Rules can require confirmation before sending. |

| **CC Utils** | Defaults off. Its Custom party invites dialog toggle finds “party invite from” anywhere in incoming chat, ignoring case. It reads the player name up to the next period and asks through a yes/no prompt. Yes sends `/p accept <player>` with spaces in the name replaced by underscores on the next gameplay update. No or timeout ignores the invite. The saved Player ping toggle sends `/ping <player>` when you hold Shift and right-click a player under the crosshair, at any distance within the loaded world. Commands have a three-second cooldown; targeting ignores block occlusion. |
| **Center Cursor** | Centers the pointer when a Minecraft screen releases mouse capture. Defaults off. |
| **Analog WASD** | Optional NuPhy Air60 HE analog movement via a Python helper and virtual controller. Requires `/dev/hidraw*` and `/dev/uinput` access; see below. Defaults off. |

Text fields support Left/Right cursor movement. Chat mods Keywords and Lobby Scanner rules show each completed comma-separated entry in a bubble; settings remain saved as comma-separated text.

### Analog WASD setup

This feature is for NuPhy Air60 HE keyboards with analog WASD travel. Keep `nuphy_analog.py` and `nuphy_distance.py` together. Stop other keyboard travel readers, then identify the device and check access:

```sh
python3 nuphy_distance.py --list
python3 nuphy_analog.py --check
python3 nuphy_analog.py
```

Run as the same normal user as Minecraft, not with `sudo`. Grant that user access to the keyboard’s `/dev/hidraw*` device and `/dev/uinput` if needed. Leave the helper running, enable **Analog WASD** in the menu, and press **Ctrl+C** to stop the helper.

## Terrain performance trial

From the source folder, close the existing launcher completely and run
`bash trace_terrain.sh`. The helper creates `Logs/`, sets an absolute trace path
and starts the launcher. The trace goes to `Logs/terrain-v8.csv` beside the helper.
It refuses to start while the usual Qt launcher is already running because that
process may reuse its old environment. Opening the launcher normally does not
enable tracing. Minecraft must start with ODIClient active before the CSV appears.

Set `ODI_TERRAIN_TRACE` to an absolute CSV path before starting the launcher.
For example, launch your usual launcher executable from a terminal with:

```sh
ODI_TERRAIN_TRACE="$PWD/terrain-v8.csv" mcpelauncher-ui-qt
```

Close an already-running launcher first so the new process inherits the variable.
Use your actual launcher command if different. Restart Minecraft after rebuilding.
The file is created only when the supported terrain hook installs; existing files
are appended with a new session header. Unset the variable for normal play.

The v8 trace records one row per second. Its session comment reports whether the
optional preparation and GL draw/buffer hooks installed (`1`) or were unavailable (`0`).
Use a new filename for v8 so its columns are not mixed with older captures.

| Columns | Measurement |
| --- | --- |
| `callbacks`, `callback_*_ms` | Existing terrain-list callback count and total/max wall time, including filtering. |
| `sections_produced`, `sections_kept` | Sections appended by the native callbacks, before and after ODIClient filtering. Counts are summed camera contributions, not unique visible sections. |
| `prepare_calls`, `prepare_wall_*_ms`, `prepare_thread_cpu_ms` | Broader terrain-preparation call: list gathering, downstream section lookup, mesh/work-list preparation and scheduling. Calling-thread CPU time helps distinguish execution from waits/preemption; worker-thread costs are not included in that CPU counter. |
| `draw_arrays_*`, `draw_elements_*` | Game GL import calls and submitted vertex/index counts. Every 64th call is timed; `samples`, `sample_total_ms` and `sample_max_ms` describe those samples only. |
| `draw_arrays_instanced_*`, `draw_elements_instanced_*`, `draw_arrays_indirect_*`, `draw_elements_indirect_*` | Supported EGL proc-address draws. Instanced counts include instances; indirect units are unknown. API timing samples use the same 1-in-64 rate. |
| `buffer_data_*`, `buffer_sub_data_*` | Game GL buffer requests, requested bytes, and sampled API timing. BufferData bytes include allocation requests with no data pointer; these are not measured PCIe transfers. |
| `frames`, `frame_avg_ms`, `frame_max_ms` | Frame count and average/max frame intervals, including pacing/VSync. |
| `outside_callback_*` | Gap from the previous client callback ending to the next beginning, measured on the same thread. Includes game/launcher work, presentation, other callbacks and waits; excludes the client callback itself. |
| `client_callback_*` | Entire ODIClient callback, including its log writes, limiter, overlays and input/module maintenance. |
| `limiter_*`, `overlays_*` | Subsets of that callback: FPS limiter and blur/HUD/menu/popup rendering block. |
| `native_submit_*` | Exact native GL renderer submission pass: CPU command traversal/driver submission plus any nested presentation/callback work; excludes command processing outside the verified caller. |
| `present_*` | Imported game EGL swap call, including any launcher/client callbacks it invokes. The actual host presentation may occur elsewhere. |
| `frame_gap_resets` | Thread changes that prevented a gap CPU-time subtraction. First observation has no previous gap. |
| `uniform_*`, `gpu_*` | Retained historical columns for retired trials; these trials are no longer selectable. |
| Trial setting and activity columns | Retained for CSV compatibility and now always zero; the six FPS trials were removed. |
| `render_options` | Current bitmask: 1=master, 2=below, 4=above, 8=horizontal. |

Each frame stage has `calls`, `wall_total_ms`, `wall_max_ms` and `thread_cpu_ms`.
Divide totals by that stage's call count for per-call averages. CPU measurements
exclude other threads/workers. Wall time minus calling-thread CPU time can include
preemption and waits; it is not a GPU execution measurement. Native submission,
presentation and client timing can nest; do not sum overlapping stages. Window
boundaries can straddle callbacks, and the current client callback finishes after
its row has been written. Zero native calls indicate uncovered/unused paths,
not free rendering. The helper does not automatically label walking phases.

Times are milliseconds except `monotonic_ns`. The broader preparation time overlaps
the existing callback; do not add them together. It does not measure the entire
renderer or all chunk rebuild workers. GL timings measure CPU-side API calls,
including possible driver stalls, not GPU execution. GL counts include entities/UI
and other game content. Supported core/OES instanced and core/EXT indirect
proc-address draws are counted too. Instanced units include every instance; indirect
units remain zero because the trace does not read command buffers. Proc pointers
cached before hook installation, multi-draw extensions, and other spellings remain
outside coverage.
ODIClient's host-renderer calls are excluded. Concurrent counters are approximate
at window boundaries. Logging and timing add overhead. Failed writes stop
measurement; installed hooks continue forwarding native behavior until exit.

Keep Motion Blur, Screen Blur, and FPS Limit off. Disable VSync/the native FPS cap
if measuring maximum throughput. At render distance 17, allow chunks to settle,
then keep the same position/direction for 20 seconds with Render OFF, 20 seconds
ON, and 20 seconds OFF again. Close the menu immediately after each toggle.
Then leave Render OFF and spend 20 seconds turning in place and 20 seconds moving
into new terrain. Share the CSV and any visible problems. Label each phase with
its approximate elapsed time. These diagnostics do not offload work or claim an
FPS improvement.

## Troubleshooting

- **ODIClient is missing from Installed Mods:** restart MCPELauncher and confirm the build installed under the launcher's mods directory. Pass the correct directory to `build.sh` if needed.
- **A feature reports an unsupported Minecraft build:** native hooks are restricted to the verified game build listed above.
- **Motion Blur is unavailable:** check that the active graphics context supports OpenGL ES 3 or OpenGL 3.3.
- **A module does not respond:** check that it is enabled in the L menu. Zoom also requires holding its configured key during gameplay.

## Credits and licenses

The bundled Inter font is licensed under the SIL Open Font License 1.1; see [`assets/OFL.txt`](assets/OFL.txt).
