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

Restart the launcher, open **Mods → Installed Mods → ODIClient**, and activate it for your profile. Launch Minecraft and press **L** to open the menu. Use the same launcher page to disable it.

A prebuilt release archive can be installed without a compiler by extracting it and running `bash install.sh` from its folder.

## Features

Open the menu with **L**. On a module tile, left-click toggles it; right-click opens its settings. Press **Escape** to return to the tiles, then press it again to close the menu. Settings use a shared glass-style switch with a left label and a sliding square X/check thumb; switching animates in both directions. Click the label or switch to toggle it. Master toggles have no enclosing bubble; faint 1-pixel white lines separate settings groups, consecutive description rows stay together, and child settings disappear when their master is off. Settings are saved beside the installed mod, except Experimental, which resets off each launch.

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
| **Environment** | Optional Time changer with a 0–23999 tick slider (6000 = noon, 18000 = midnight), plus Fog color with Hue (0–360°), Saturation and Value (0–100%) sliders. Changes local visuals only; server time stays unchanged. Defaults off; settings persist. |
| **Render limits** | Experimental vertical limits and a 16–256 block horizontal terrain radius. Can hide visible terrain; defaults off. |
| **AutoGG** | Sends a configurable response to matching incoming chat. Defaults to matching “You won the game” and sending “gg”. |
| **Chat mods** | Optionally hides incoming messages that match a comma-separated keyword list. |
| **Lobby Scanner** | Runs configured commands or sends messages when matching players join. Rules can require confirmation before sending. |
| **CC Utils** | Defaults off. Its Custom party invites dialog toggle finds “party invite from” anywhere in incoming chat, ignoring case. It reads the player name up to the next period and asks through a yes/no prompt. Yes sends `/p accept <player>` with spaces in the name replaced by underscores on the next gameplay update. No or timeout ignores the invite. |
| **Center Cursor** | Centers the pointer when a Minecraft screen releases mouse capture. Defaults off. |
| **Experimental** | Offers a test prompt. Its popup can also be used by other modules; Experimental itself is not required for those prompts. Defaults off. |
| **Analog WASD** | Optional NuPhy Air60 HE analog movement via a Python helper and virtual controller. Requires `/dev/hidraw*` and `/dev/uinput` access; see below. Defaults off. |

### Analog WASD setup

This feature is for NuPhy Air60 HE keyboards with analog WASD travel. Keep `nuphy_analog.py` and `nuphy_distance.py` together. Stop other keyboard travel readers, then identify the device and check access:

```sh
python3 nuphy_distance.py --list
python3 nuphy_analog.py --check
python3 nuphy_analog.py
```

Run as the same normal user as Minecraft, not with `sudo`. Grant that user access to the keyboard’s `/dev/hidraw*` device and `/dev/uinput` if needed. Leave the helper running, enable **Analog WASD** in the menu, and press **Ctrl+C** to stop the helper.

## Uniform reuse v2 trial

Restart Minecraft, then open **L → Render → Uniform reuse v2 (trial)**. This switch
is independent of terrain limits and defaults OFF each launch. It avoids repeated
native vector/integer shader uploads when a small uniform's bytes are identical to the last
upload at that location in the current program interval. It changes CPU driver
submission rather than terrain distance or shader arithmetic; it is not CUDA or
tensor-core offloading. Matrix uploads remain native because the local benchmark did not show a reliable
benefit. The first version was withdrawn after an in-game FPS regression. V2 moves
EGL context checks to frames/program binds and tracks native context switches,
compares words rather than individual bytes, and uses frame-thread local counters.
Vector/integer submission improved in a local software-GL benchmark; RTX 5060
in-game FPS remains unproven.

This is a more invasive trial than GPU multi-draw. It relies on the verified game
uniform imports covering native writes; unobserved shader writes or context switches from other code
could invalidate that assumption. The cache resets on frames, program binds and tracked EGL context switches,
link/delete operations, toggles, noneligible array writes and context/thread
changes. Turn the switch OFF if lighting, textures, water or entities look wrong.

Close the launcher and start `bash trace_terrain.sh`. Leave **Render distance**
and **GPU multi-draw (trial)** OFF. With the camera fixed and menu closed, record
20 seconds with Uniform reuse v2 OFF, 20 seconds ON, then 20 seconds OFF. Send
`Logs/terrain-v6.csv` and describe any visual changes. `uniform_calls` establishes
whether the intercepted path ran; `uniform_skipped_calls` establishes whether
reuse occurred. The trace includes toggle, availability and status columns, plus counts of eligible
updates, unsupported writes and calls rejected for their thread/context.

## GPU multi-draw trial

Restart Minecraft after rebuilding. Open **L → Render → GPU multi-draw (trial)**.
This independent switch defaults OFF each launch; leave **Render distance** OFF
when comparing it. The trial replaces a verified CPU loop with one GPU indirect
multi-draw call for eligible batches of four or more commands. It uses the existing
OpenGL ES renderer and does not use CUDA or tensor cores. It does not change chunk
meshing, terrain distance or geometry. Unsupported contexts and batches keep the
native loop. The game may already use a different fast path, so a gain is not assured.

Launch with `bash trace_terrain.sh`, then stand in the same spot for about 20 seconds
OFF, 20 seconds ON, then 20 seconds OFF. Close the menu during each measurement.
Keep the camera, render distance and FPS/VSync settings consistent. Repeat while
moving if the image remains correct. Send `Logs/terrain-v6.csv` with any visual
issues. `gpu_avoided_calls` above zero shows that the substitution ran; zero means
this capture provides no evidence of acceleration. `gpu_status` explains whether
the driver/context supports the trial. Driver-state checks add some CPU cost, so
FPS must be compared even when calls were replaced.

## Broader frame profiling

The same `bash trace_terrain.sh` helper now records `Logs/terrain-v6.csv`.
Uniform reuse v2 remains available and defaults OFF each launch. Its v5 capture
showed an apparent small FPS gain, which still needs an OFF/ON/OFF confirmation.

Keep **Render distance** and **GPU multi-draw (trial)** OFF. In a loaded world,
stand in one spot, let loading settle, then close the menu and record 30 seconds
with **Uniform reuse v2 OFF**, 30 seconds **ON**, then 30 seconds **OFF**. Toggle
that exact switch between phases. Afterwards turn v2 ON and walk around for about
30 seconds. Send the CSV and mention whether anything looked wrong. Keep native
render distance, VSync and other settings unchanged throughout. FPS limiting can
hide a gain; the new limiter/presentation measurements help expose that.

V6 adds wall and calling-thread CPU time for the native GL submission stage,
EGL presentation, the whole ODIClient callback, its overlays and FPS limiter,
and the interval between one callback finishing and the next starting. It also
reports whether each optional native hook installed. These are passive timings;
no native work is skipped by profiling.

## Terrain performance trial

From the source folder, close the existing launcher completely and run
`bash trace_terrain.sh`. The helper creates `Logs/`, sets an absolute trace path
and starts the launcher. The trace goes to `Logs/terrain-v6.csv` beside the helper.
It refuses to start while the usual Qt launcher is already running because that
process may reuse its old environment. Opening the launcher normally does not
enable tracing. Minecraft must start with ODIClient active before the CSV appears.

Set `ODI_TERRAIN_TRACE` to an absolute CSV path before starting the launcher.
For example, launch your usual launcher executable from a terminal with:

```sh
ODI_TERRAIN_TRACE="$PWD/terrain-v6.csv" mcpelauncher-ui-qt
```

Close an already-running launcher first so the new process inherits the variable.
Use your actual launcher command if different. Restart Minecraft after rebuilding.
The file is created only when the supported terrain hook installs; existing files
are appended with a new session header. Unset the variable for normal play.

The v6 trace records one row per second. Its session comment reports whether the
optional preparation and GL import hooks installed (`1`) or were unavailable (`0`).
Use a new filename for v6 so its columns are not mixed with older captures.

| Columns | Measurement |
| --- | --- |
| `callbacks`, `callback_*_ms` | Existing terrain-list callback count and total/max wall time, including filtering. |
| `sections_produced`, `sections_kept` | Sections appended by the native callbacks, before and after ODIClient filtering. Counts are summed camera contributions, not unique visible sections. |
| `prepare_calls`, `prepare_wall_*_ms`, `prepare_thread_cpu_ms` | Broader terrain-preparation call: list gathering, downstream section lookup, mesh/work-list preparation and scheduling. Calling-thread CPU time helps distinguish execution from waits/preemption; worker-thread costs are not included in that CPU counter. |
| `draw_arrays_*`, `draw_elements_*` | Game GL import calls and submitted vertex/index counts. Every 64th call is timed; `samples`, `sample_total_ms` and `sample_max_ms` describe those samples only. |
| `buffer_data_*`, `buffer_sub_data_*` | Game GL buffer requests, requested bytes, and sampled API timing. BufferData bytes include allocation requests with no data pointer; these are not measured PCIe transfers. |
| `frames`, `frame_avg_ms`, `frame_max_ms` | Frame count and average/max frame intervals, including pacing/VSync. |
| `outside_callback_*` | Gap from the previous client callback ending to the next beginning, measured on the same thread. Includes game/launcher work, presentation, other callbacks and waits; excludes the client callback itself. |
| `client_callback_*` | Entire ODIClient callback, including its log writes, limiter, overlays and input/module maintenance. |
| `limiter_*`, `overlays_*` | Subsets of that callback: FPS limiter and blur/HUD/menu/popup rendering block. |
| `native_submit_*` | Exact native GL renderer submission pass: CPU command traversal/driver submission plus any nested presentation/callback work; excludes command processing outside the verified caller. |
| `present_*` | Imported game EGL swap call, including any launcher/client callbacks it invokes. The actual host presentation may occur elsewhere. |
| `frame_gap_resets` | Thread changes that prevented a gap CPU-time subtraction. First observation has no previous gap. |
| `uniform_*` | Uniform reuse toggle/capability state, intercepted uploads, skipped duplicate uploads/bytes, and status. Counters are active only during tracing. |
| `gpu_*` | Trial toggle/capability state; substituted array/index batches and commands; avoided native calls; batches left on the native loop while enabled; current capability status. Counts are drained once per trace row. |
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
and other game content; extension/proc-address draw paths can bypass these imports.
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
