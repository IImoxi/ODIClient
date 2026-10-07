# Project context

This is a Linux MCPELauncher mod for Minecraft Bedrock, growing into a small
client in the style of Flarial. The owner uses Arch Linux on Wayland. More simple modules will be added over time. Prefer small, direct
changes that fit the existing code; introduce a module framework only when
actual features need one.

Read this file first, then only the source and tests relevant to the task.
`README.md` contains detailed user setup, troubleshooting, and distribution
instructions. Keep this map current when architecture, files, or commands change.

## Current behavior and platform

- The shared `popup.h` API accepts copied title/message and an optional answer
  callback, and supports one pending prompt. Hold left click for one second for Yes or
  right click for one second for No; both show the skin-download progress ring.
  All input passes through. Early release, conflicting buttons, or focus loss
  cancel holds. Notifications draw above the menu and remain across game GUIs.
  They share Tablist background, blur, border, radius, and a 500 ms slide/fade
  entering from the right. Dimensions are 70% of the original; the header keeps
  its size, with smaller body text and a close header gap. Yes and No are
  evenly spaced at rest, with equal padding below the body and above the bottom.
  Long body text wraps to a second line and increases the popup height. A hold slides its label horizontally to the center,
  fades the other label, and reveals its ring over 180 ms; release restores both.
  A faint bottom bar fills over the timeout (10 seconds by default; optional
  1–3600 second API argument), then closes the popup and calls back with No.
  The timer starts on the first frame and continues across focus loss/menus.
  Completed answers hide all other content; only Yes or No moves to the dialog
  center and scales to title size over 500 ms using the title-size atlas, holds
  for another 500 ms, then dismisses.

- **Tablist** defaults ON; hold Tab (launcher key 9) for a passive player roster
  with a quarter-resolution background blur refreshed at most 30 Hz,
  received-skin heads/hat layers, compact borderless rows, smaller rounded
  corners, and the menu's 500 ms exponential fade/slide animation. Column/row size
  changes animate over 500 ms. The first scroll selects the top-left player and
  opens a fixed 20-degree full-body skin preview in a new rightmost column (or
  replaces the third name column). Further scrolling selects down columns through an overlapping column window,
  retaining the last two name columns at the end. A horizontal menu-style scrollbar
  with 180 ms ease-out quart thumb motion appears when roster plus preview requires
  more than three columns. Selection has a faint white 1-pixel outline, 2px corners, and no fill.
  Selection follows UUID across roster/presence reordering; if the selected player
  leaves, select the next player (previous if last), or clear when the roster is empty. Releasing Tab resets selection. With a selection, hold right click for one
  second to fill a ring and save the original RGBA skin as a timestamped PNG in
  `ODIClient/skins/`; worker-thread saves report results only in preview mode.
  Holds cancel on release, selection/skin changes, focus loss, or closing Tab;
  consumed right-click releases stay consumed. No instructions in the default roster. It draws after blur and before the menu, keeps camera/mouse capture,
  and closes on focus loss, Minecraft GUIs, or custom-menu visibility. Exact-build
  hooks observe PlayerList, PlayerSkin, StartGame, and Disconnect dispatchers while
  always forwarding originals. Copied UUID/name/head/full RGBA skin data is protected
  by a short lock; no live native objects survive dispatch. World lifecycle and
  handler identity changes reset the roster. Cache ceiling 4096; at most eight
  names per column, with narrower columns and no player row bubbles. Full textures
  use a 64 MiB LRU budget; names/heads survive texture eviction. The preview uses
  standard cuboids and outer layers; slim arms are inferred from unused alpha
  strips, legacy limbs mirrored. Custom geometry/capes are not rendered. Flarial
  presence uses a background lookup; those players sort first and get a red [FL]
  badge. The tablist has no footer. Standard
  64/128/256 and legacy half-height skins are supported, initials otherwise.
  No platform/input-device indicator or external skin service.
- **Particles** defaults OFF, saved in `odiclient.conf`. Native GameMode and
  SurvivalMode attack wrappers spawn local critical-hit particles around player
  targets during focused gameplay. Normal reach/target selection stays native;
  effects follow attack attempts, including server-rejected attempts. Mobs,
  blocks, and air swings are excluded. No actor pointers survive the callback.
  Uses the exact Minecraft profile and shared hook manager; appearance needs
  in-game verification.
- **Environment** defaults OFF with saved Time changer and Fog color toggles.
  Time uses 0–23999 ticks (default 6000) to override the Overworld celestial-angle
  calculation without changing world/server time. Fog overrides the native Dimension
  fog RGB in Overworld, Nether and End using Hue 0–360, Saturation/Value 0–100
  (defaults 0/0/100), preserving native alpha. Child sliders appear only when
  their toggle and the module are enabled. Physically inspired sky is a saved
  default-OFF control. Verified shader/program/draw services replace a matched,
  linked native Sky draw with one full-screen triangle using the native camera
  matrix. Its own single-scattering atmosphere, sun, moon and stars replace the native celestial draws only after a replacement in the same
  frame/context. Native mesh colors and clear color are untouched. The sky
  forwards native sun/moon draws when the saved Vanilla sun/moon child
  toggle is ON (default OFF); procedural disks/glow then disappear. The custom sun has a soft angular glow. Clouds are removed; the old saved cloud slot remains for config compatibility. Sky controls share a group without intervening text.
  Sky always uses a 256×256 RGBA16F GPU atmosphere lookup, half-resolution
  atmosphere, and 6×3 integration samples (formerly 8×4). There are no quality
  toggles; legacy saved flags are ignored by the renderer. Detailed celestial
  effects stay full resolution. Lookup refreshes after angle changes over
  1/4096 of a day or sample changes; unsupported texture paths retain direct
  rendering with reduced samples. Resources are bounded to four contexts per
  thread and remain owned by those GL contexts. The shader skips invisible
  star work and stops sunlight integration for shadowed samples; no cloud
  noise or clock reads remain.
  Disabling the sky
  restores the original shader path. Both standard and instanced layouts are
  supported. Environment distinguishes matched source, compilation, linked
  program and an observed replacement draw. Missing/invalid camera matrices or
  unsupported paths forward native draws. GLES pixel tests cover both layouts,
  rotation, infinite-far projection, constant native colors, foreground depth and
  GL state restoration. In-game routing, camera/sun alignment and performance
  still need verification. Exact-build Dimension hooks preserve time/fog behavior.
- Failed buffer-reuse, occlusion, weather/particle suppression, uniform-reuse,
  and multi-draw implementations and their tests/native profiles are removed.
  Historical trace columns remain zero-filled for CSV compatibility.
  `gpu_shader_services` owns only shader-source/compile and link/delete/use-program
  hooks used by Environment; uniform/EGL cache hooks and cache frame work are absent.
- **Render** provides experimental below/above-camera terrain limits, all OFF by
  default. An optional horizontal terrain radius also defaults OFF at 128 blocks.
  Distances are 16–256 blocks (defaults 64 below, 128 above), persisted
  in `odiclient.conf`. It filters whole 16-block sections before mesh preparation;
  sections intersecting the range remain. Horizontal limits retain sections whose
  X/Z bounds intersect the radius, including tangent sections. This can hide visible cliffs/caves.
  Existing frustum checks, native bookkeeping and multi-camera merging remain.
  Its settings group Horizontal, Below, and Above switches and conditional
  sliders under the Render distance master toggle, separated by faint dividers.
  Owner reports ~6% FPS gain with aggressive vertical limits; appearance requires
  in-game verification.
  Terrain diagnostics are opt-in through `ODI_TERRAIN_TRACE=/absolute/path.csv`.
  The existing Render hook counts sections and aggregates callback wall time even
  with the module OFF. Optional exact-build preparation-call, four GL import hooks and shared
  instanced/indirect EGL proc wrappers add broader wall/calling-thread CPU timing, draw counts, requested buffer
  bytes and 1-in-64 sampled API timings. `render_trace_frame()` drains summaries
  once per second. Timings overlap and do not measure GPU execution; GL coverage
  includes game-wide imports and supported instanced/indirect proc calls, excluding
  pointers cached before interception and multi-draw extension calls. No diagnostic hooks install without the opt-in.

- Press **L** to toggle the custom menu. Escape returns from a page to its tiles,
  then closes it. The rounded panel uses a 500 ms exponential ease-out animation;
  input capture begins halfway through opening (250 ms) and ends as soon as closing starts.
  Cursor capture changes on the frame thread; captured menus block camera movement.
  FPS Limit, Motion Blur, and AutoGG keep running while the custom menu is open;
  blur is applied before drawing the menu so the menu stays sharp.
  Entering settings fades contents in over 220 ms while they zoom from 75% to 100% around the panel center using ease-out quart. Leaving settings uses ease-in quart to shrink those contents from 100% to 75% and fade them out; the module grid starts fading in after 110 ms. The outer panel stays fixed during settings transitions. The base title keeps its size and opacity; `ODIClient` slides left over 220 ms while ` - <page>` fades in, forming a centered title. On exit the suffix disappears and the base title slides back to center.
  The menu uses a 480x360 logical reference and common fitting scale tiers. Tiles are
  centered 3:4 portrait cards with a subtle divider between icons and labels. The
  entire panel background is blurred at quarter resolution, refreshed at most 30 times per second, with a 70% dark tint; the unblurred title bar is drawn on top. Button backgrounds have separate normal, enabled, hover, and enabled-hover colors/opacities in
  `menu_style.h`, with independent 180 ms ease-out quart transitions in both directions; main/settings colors and outline sizes are independent.
  Icons render white without an accent tint.
  The color change separates the title and content without a divider line.
  Text is rasterized at its integer display height (up to 60px), using a bounded
  16-atlas cache; all settings text uses settled-size glyphs scaled continuously
  with the page in both animation directions. Slider fitting/wrapping uses settled
  dimensions; description rows align with toggle label padding.
  Descriptions use 2% of panel height for text, compact 3% rows and 4% row spacing.
  Larger text scales
  the 60px atlas. The menu has antialiased rounded borders, consistent header/footer spacing,
  and mouse-wheel scrolling for tiles and settings as more modules are declared, with a 180 ms ease-out quart animation and clipped content. Enable
  toggles reveal dependent settings: Motion Blur mode and its Strength
  or Target Hz, FPS Limit, and AutoGG trigger/response fields. Each implemented
  module has a master Enable toggle on its settings page. All boolean settings
  use the shared `MenuPage::toggle()` switch: left label, subtly rounded glass
  track, square X/check thumb, and reversible 180 ms slide/icon crossfade.
  Master toggles have no background bubble; visible settings rows are separated
  by faint white 1-pixel dividers using the shared `draw_gl_divider()` API.
  Consecutive descriptions or sliders in the same parent group have no internal divider;
  `.groupWithPrevious()` can also join other related controls. Toggle and slider
  rows share compact 9% heights and 11% spacing. Child toggle labels and switch
  right edges align with child sliders; each Render toggle/slider pair is grouped.
- **Zoom** defaults OFF; hold C when enabled, scroll to adjust 1.5x–30x (initial 3x).
  Its menu key-binding control captures a new key; L/Escape are reserved. Enabled
  state, binding, default magnification, and scroll sensitivity persist. Default zoom slider uses
  1.5x–30x (default 3x); scroll adjusts only the current hold. Scroll step slider
  uses 0.1x–5x per wheel step (default 0.5x). Slider labels format the saved
  tenths as multipliers. Reversing at either limit takes effect on the first step. Uses four verified
  native camera FOV option call sites, without changing saved FOV or HUD size.
  Clears on release, disable, focus loss, or menus; unsupported builds fail closed.
- **Auto Sprint** simulates Left Ctrl (launcher key code 17) while W is held, respecting Shift, physical
  Ctrl, focus, and gameplay state. It assumes default Minecraft bindings.
- **Motion Blur** blends previous frames, defaults to 30% strength, and accepts
  0–80%. Its master toggle contains a Trail/FPS average mode choice; only the
  selected mode shows Strength or the 30–500 Hz Target Hz slider. FPS averaging
  works independently of saved Trail strength; averaging keeps up to 16 frames (current plus 15 saved),
  allocating history as needed; temporal blur pauses when a frame interval exceeds
  the selected window. It needs an EGL context with GLES 3 or desktop OpenGL 3.3.
- **Experimental screen blur** spatially blurs the whole finished game frame,
  including inventory, HUD, and other screens. The custom menu stays sharp.
  Defaults OFF; shares the Motion Blur GL adapter.
- **FPS Display** defaults OFF and displays average FPS during focused rendering,
  including menus. Optional 1% low appears alongside FPS and uses the reciprocal
  of the mean slowest ceil(1%) frame times in the same window. Average/update is
  250–2000 ms in 250 ms steps (default 1000 ms). Font scale uses the shared
  0.5x–6x tiers (default 1x). The shared `MenuPage::anchor()` control draws a 16:9
  rectangle with four selectable/hoverable corner dots (default top-left).
  The shared `display_layout.h` API stacks visible displays at each corner in
  stable draw order: downward at top corners, upward at bottom corners, with
  8px margins and 4px gaps. All settings persist in `odiclient.conf`; old intervals
  migrate to the closest allowed step.
  Focus loss, disabling, and interval/low changes reset sampling; startup shows
  `--` until a complete window. Reuses the frame limiter monotonic timestamp and
  shared font renderer, without native hooks. At most 32768 frame times are stored
  per window; exceeding that keeps average FPS exact and shows `--` for the low.
- **FPS Limit** optionally paces Minecraft frames from 30–480 FPS, including
  inventory screens and the custom menu. Minecraft's own
  FPS setting and VSync can still impose a lower limit.
- **Analog WASD** optionally uses NuPhy Air60 HE key travel through a separate
  Python helper and virtual Xbox 360 controller. No third-party Python packages.
  This feature is low priority and the owner is considering removing it; do not
  expand it unless requested.
- **Chat mods** defaults OFF with a saved Message blacklist toggle and a conditional
  comma-separated Keywords text box. A listener on the shared `chat.h` API hides
  matches before native display, ignoring ASCII case, formatting codes, surrounding
  spaces/newlines, and empty entries. Keywords supports Shift+Enter for saved
  multiline input; commas remain the keyword separator. AutoGG still observes hidden messages. Local echoes and
  unsupported payloads pass through. Settings use the existing backend; no new hook.
- **AutoGG** matches configurable incoming chat (default `You won the game`) and
  sends a configurable response (default `gg`) through the native chat packet sender
  without opening chat, including the normal local echo in integrated-server worlds.
  Settings report sends and skipped matches. Trigger, response,
  and enabled state persist in `odiclient.conf` beside the installed binary.
  It includes command messages (`/tell`, `/say`) and player chat, and ignores
  duplicate matches within 15 seconds.
  Text fields live in the AutoGG page; there, L is passed to text input.
  Menu textboxes use launcher Shift/punctuation codes, support Caps Lock and
  repeats, and show a blinking crimson caret with 160 ms exponential ease-out movement. Multiline is opt-in through the
  `textBox(..., multiline)` argument and expands by one text line per
  inserted newline, with the text block centered vertically.
  Left/Right move the edit cursor. An optional comma-bubble argument draws
  completed comma-separated entries as individual rounded labels while keeping
  the saved comma-delimited text; Chat mods and Lobby Scanner use it.
- **Lobby Scanner** defaults OFF. It matches newly added roster players against
  comma-separated `player/command` rules (for example `steve/hub,alex/home`) and
  submits a command through MinecraftCommands once per join. Use `player#/message` to send
  literal public chat text. Both modes use the shared chat backend.
  `player?/command` and `player?#/message` ask through the shared popup before
  sending. No/cancel/10-second timeout skip the action. One confirmation is
  pending at a time; a busy popup or pending confirmation skips later joins.
  Yes queues copied text, submitted once on the next live ClientInstance callback
  during focused gameplay, without waiting for an incoming packet. Only numeric
  handler/client identity and copied text survive dispatch. Rules edits, disabling, world
  resets, and handler changes cancel pending actions. The popup does not require
  Experimental. Name fragments match without ASCII case; toggle and rules persist in `odiclient.conf`.
- **CC Utils** defaults OFF with a separate Custom party invites dialog toggle.
  It searches the whole chat message for `party invite from`, ignoring ASCII case,
  takes the player name up to the next period, asks through the shared popup,
  and submits `/p accept <player>` on the next live gameplay callback after Yes,
  replacing spaces with underscores without waiting for another incoming chat message.
  Successful prompts hide the original message. Names accept up to 64 ASCII letters,
  digits, underscores, hyphens or spaces; a busy popup leaves chat visible.
  Disabling, world resets, and handler changes cancel pending acceptance.
  The saved Player ping toggle sends `/ping <player>` on a focused gameplay
  Shift/right-click of a loaded player under the crosshair, without interaction
  reach limits; targeting ignores block occlusion. A passive click queues one lookup for the next live callback,
  expires after 250 ms, and uses a three-second cooldown across world/toggle changes.
  All CC Utils toggles persist in `odiclient.conf`.
- Zoom, Lobby Scanner, AutoGG, Auto Sprint, Motion Blur (including FPS-based averaging, target Hz,
  and screen blur), and FPS Limit settings persist in `odiclient.conf`. Existing
  `autogg.conf` files are loaded and migrated when the new file is absent. Tablist font choice persists in `odiclient.conf`. FPS
  Limit defaults to OFF at 120 FPS. Analog WASD defaults OFF.
- **Center Cursor** defaults OFF and persists in `odiclient.conf`. It centers
  the pointer once when gameplay mouse capture is released by a Minecraft GUI
  or the custom menu, using SDL window coordinates (including HiDPI) or X11.
- Current target: launcher **1.8.4**, **x86_64**, **SDL3/Wayland** and
  **EGLUT/X11** (including XWayland). SDL physical-key/focus reads and native
  launcher keyboard callbacks support sprint and zoom on native Wayland;
  the X11 adapter remains the fallback. Real gameplay still needs user verification.

## File map

Tests are in `tests/`. Maintainer API notes and native research are in `docs/`.

| Files | Purpose / when to read |
| --- | --- |
| `client.cpp`, `client_modules.h` | Mod entry points, launcher callbacks, atomic module state and typed module controls, cursor and gameplay coordination. Start here for module lifecycle wiring. |
| `client_settings.h` | Shared saved-setting API for zoom, sprint, blur, FPS limit, Lobby Scanner, and related modules; implemented by the existing persistence backend in `auto_gg.cpp`. |
| `runtime.cpp` | Hidden freestanding `memset` and `memcpy` primitives for the Android mod; no linked libc. |
| `launcher_api.h` | Local declarations of the launcher's game-window C ABI. |
| `native_cursor.h` | Version-sensitive native GameWindowHandle/vtable bridge for cursor capture. |
| `native_input.h` | Launcher 1.8.4 x86_64/libstdc++ GameWindow callback and string bridge for synthetic keyboard/text input. |
| `minecraft_build.h` | Central Minecraft ELF build ID, AutoGG chat/command packet ABI addresses, Zoom sites, and signatures for the supported game build. |
| `hook_manager.h`, `hook_manager.cpp`, `tests/test_hook_manager.cpp` | Shared game discovery/build and mapping checks, owned eight-byte patch batches, page protections, rollback, and nearby relay allocation. AutoGG, Zoom, Render, Tablist, and Particles use it. |
| `auto_gg.cpp`, `auto_gg.h` | Version-gated TextPacket dispatcher hook, formatting removal, trigger/response, native chat sender and MinecraftCommands execution bridge, Lobby Scanner commands/confirmation queue, CC Utils party invite confirmation, CC Utils deferred player ping/cooldown, additional live dispatcher hooks, cancellation, saved settings, and Chat mods blacklist filtering. |
| `player_target.cpp`, `player_target.h`, `tests/test_player_target.cpp`, `docs/research/player_target_research.md` | Exact-build camera ray and borrowed loaded-player lookup without interaction reach limits; used by CC Utils player ping during shared live-client callbacks. |
| `chat.cpp`, `chat.h`, `tests/test_chat.cpp` | Shared incoming-chat and live-client listener APIs; AutoGG, Chat mods and CC Utils register before gameplay. All listeners observe messages even when hidden; optional after callbacks run after native forwarding/suppression. AutoGG owns the shared TextPacket and verified LocalPlayer-getter hooks, plus a bounded local diagnostic queue that calls the exact-build native chat UI presentation helper without packet receive or network submission. |
| `autosprint.cpp`, `autosprint.h` | SDL3/X11 symbol loading, physical keys, focus, synthetic keys, movement releases, and cursor centering. |
| `motion_blur.cpp`, `motion_blur.h`, `panel_renderer.h` | Shared host EGL/GL adapter, frame history, whole-screen blur, and general `draw_gl_panel()` primitive using `PanelPaint` and 1px `draw_gl_divider()` helper; preserves GL state and antialiases rounded fills/outlines. |
| `custom_menu.cpp`, `custom_menu.h` | Menu builders/model, scrolling tiles and controls, text input/caret and optional multiline fields, key-binding capture, layout/animation helpers, frame snapshots, and drawing. `custom_menu_is_visible()` includes animations; `custom_menu_captures_input()` governs input/cursor/gameplay. |
| `docs/CODE_QUALITY.md` | Architecture review, cleanup summary, and concrete growth/persistence maintenance concerns. |
| `docs/SHARED_APIS.md` | Internal API reuse guide, examples, threading/lifetime constraints, and candidates for future extraction. |
| `ui_animation.h`, `tests/test_ui_animation.cpp` | Stateless shared quartic/exponential easing for menu, Tablist and popup; no linked runtime dependency. |
| `menu_style.h` | Menu, scrollbar, slider, and Tablist colors, opacities, blur tint/strength, corner radii, panel spacing, and animation/input timing. |
| `menu_pages.cpp` | Custom-menu page and tile declarations using `newPage()` and `newTile()`. |
| `custom_font.cpp`, `custom_font.h`, `assets/inter.ttf`, `assets/icon-*.png` | Host FreeType text atlas (Tablist can also select the latest launcher Mojangles font), PNG icon loading, shared GL drawing, 16x16 RGBA tablist-head uploads, offscreen fixed-view 3D skin rendering, and hold-progress ring. |
| `palette.json` | Custom menu color palette reference. |
| `zoom.cpp`, `zoom.h`, `tests/test_zoom.cpp` | Build-ID/signature-gated camera FOV reads, per-thread read-only option proxies, optics, and executable hook tests. |
| `fps_display.cpp`, `fps_display.h`, `tests/test_fps_display.cpp` | Bounded frame-time averaging, optional 1% low, and passive anchored text overlay. |
| `display_layout.cpp`, `display_layout.h` | Frame-thread EGL dimensions and per-corner vertical stacking for HUD displays; reset once before rendering displays. |
| `ui_scale.h` | Shared scale tiers used by menu fitting and display fonts. |
| `fps_limiter.cpp`, `fps_limiter.h` | Host monotonic timer loading and software frame pacing. |
| `skin_image.cpp`, `skin_image.h`, `tests/test_skin_image.cpp` | Host skin allocation independent of export setup, mapped-ELF path lookup (not host dladdr), and asynchronous lossless PNG export to the mod root `skins/` folder; dynamic libc/zlib loading and export checks. |
| `tablist.cpp`, `tablist.h`, `tests/test_tablist.cpp` | Native roster/skin/world lifecycle observers, copied player cache, Lobby Scanner join notifications, Tab/scroll/right-click input, selected-skin preview, animated dimensions and overlay, saved master toggle, and native/layout checks. |
| `flarial_presence.cpp`, `flarial_presence.h`, `tests/test_flarial_presence.cpp` | Optional host curl/json-c background presence lookup for Tablist badges; no linked runtime dependency. |
| `docs/research/lobby_scanner_research.md` | Exact-build evidence for additional typed dispatchers used for deferred confirmed sends. |
| `docs/research/sky_research.md` | Native sky interfaces, full-screen replacement evidence and remaining in-game checks. |
| `docs/research/rea_mod_candidates.md` | REA smoke-test outcome and bounded static leads for camera controls, brightness, latency, coordinate copying, and durability. |
| `popup.cpp`, `popup.h`, `tests/test_popup_render.cpp` | Shared thread-safe yes/no popup API, copied text, callbacks, passive input, drawing above GUIs, and headless pixel checks. |
| `particles.cpp`, `particles.h`, `tests/test_particles.cpp`, `docs/research/particles_research.md` | Player attack effects, native critical emitter, behavior/ABI gates, and investigation evidence. |
| `environment.cpp`, `environment.h`, `tests/test_environment.cpp` | Local celestial-angle and HSV fog overrides, exact-build Dimension hooks and behavior checks. |
| `sky_renderer.cpp`, `sky_renderer.h`, `tests/test_sky_renderer.cpp` | Full-screen camera-directed sky with procedural atmosphere, sun, moon and stars; one-degree horizon blend, GPU atmosphere reuse across tiny time changes; linked-program/draw gates, per-thread/context metadata, same-frame native celestial suppression and real GLES pixel checks. |
| `render.cpp`, `render.h`, `tests/test_render.cpp`, `docs/research/render_research.md` | Experimental vertical/horizontal terrain-list filtering, opt-in v8 terrain/preparation CSV timing, executable hook/ABI checks, and native investigation evidence. |
| `gpu_shader_services.cpp`, `gpu_shader_services.h`, `tests/test_gpu_shader_services.cpp` | Exact-build shader-source/compile and link/delete/use-program observer service for Environment; always forwards native calls, with no uniform-reuse trial. |
| `render_frame_trace.cpp`, `render_frame_trace.h`, `tests/test_render_frame_trace.cpp` | Opt-in v8 wall/calling-thread CPU stages: callback gaps, client, limiter, overlays, exact native GL submit vtable and game EGL swap import. Nested stages overlap; no work is skipped. |
| `render_gl_trace.cpp`, `render_gl_trace.h`, `tests/test_render_gl_trace.cpp` | Shared direct-draw and EGL proc-lookup owner with optional draw substitution; native forwarding otherwise. Opt-in imported GL draw/byte counts and sampled API timing use separate buffer hooks. |
| `analog_input.cpp`, `analog_input.h` | Nonblocking mod/helper IPC, peer identity, status, mixed-input startup flag. |
| `nuphy_analog.py` | Analog mapping, Unix socket server, virtual controller via `/dev/uinput`, neutralization and output throttling. |
| `nuphy_distance.py` | NuPhy `/dev/hidraw*` device discovery, reporting protocol, travel decoding; imported by the analog helper. |
| `api_stubs.cpp` | Build-only launcher API placeholder libraries; never install these stubs. |
| `build.sh`, `install.sh`, `tests/test_build_install.sh` | Compile/package/install from this source tree; build ID fingerprints actual inputs and compiler. Installer verifies SHA-256 against `build-info.txt` and byte equality. Build automatically runs install. |
| `trace_terrain.sh`, `Logs/` | Source-folder diagnostic launcher helper and local terrain CSV captures; close the existing Qt launcher before starting the helper. Logs are personal trial data, not distribution assets. |
| `1.0.0/x86_64/mod.json` | Launcher mod identity, version, architecture, and description. |
| `1.0.0/x86_64/` | Runtime package: compiled mod plus installed copies of helpers and README. Edit root sources, then rebuild/install. |
| `tests/test_sdl_input.cpp`, `tests/test_native_build.py` | SDL focus/sprint/cursor regression checks and checks against the actual Minecraft ELF. |
| `tests/test.cpp` | Mock launcher/X11 checks for the L custom-menu route, cursor, and sprint behavior; includes stubs for other modules. |
| `tests/test_analog.py`, `tests/test_analog_input.cpp` | Helper mapping/protocol checks and native IPC integration checks. |
| `tests/test_custom_menu.cpp` | Real headless EGL/GLES checks for antialiasing, animation/input timing, key/mouse releases, tile/settings scrolling, callback reentry, conditional group layout/hit areas, hidden focus/drag cleanup, slider extremes, registration limits, and layout bounds. Includes the menu source. |
| `tests/test_custom_font.cpp` | Real headless EGL/GLES checks for custom-menu text/icon pixels, inherited clipping and color masks, texture uploads, and GL state restoration. Includes the renderer source to inspect atlas pixels. |
| `tests/test_motion_blur.cpp` | Real headless EGL/GLES checks for pixels, state, and history lifecycle. |
| `tests/test_auto_gg.cpp` | AutoGG checks, including native sender bridge, local/remote chat echo, packet layouts, cooldown/cancellation, saved settings, and build-ID/vtable gate. Includes the module source to exercise its private bridge. |
| `skins/` | Local timestamped skin exports; do not ship personal exports in distribution archives. |
| `build/`, `__pycache__/` | Generated files, not source. |

## Architecture constraints

- New features that require native hooks must expose a small, typed shared API
  owned by the hook backend. Keep feature behavior in its module and let other
  modules subscribe to observers or call the service through that API. Reuse
  existing hook APIs whenever the interception point is already owned; extend
  the owning API if necessary instead of installing another hook. Reuse existing
  UI APIs and controls (MenuPage builders, popup, font, panel, and animation
  helpers) for existing elements. Document new APIs in `docs/SHARED_APIS.md`
  with threading and borrowed-object lifetime constraints. Keep APIs scoped to
  actual features; this does not require a general module framework.

- `mod_preinit()` enables mixed input and registers window creation. Once a window
  exists, `onWindowCreated()` attaches keyboard, mouse, and swap-buffer callbacks.
  `onFrame()` coordinates cursor, rendering, sprint, analog input, and menu state.
  `mod_init()` initializes AutoGG, Zoom, shared shader/program services, Render, Tablist, Particles, and Environment after Minecraft loads.
- Menu callbacks can run on keyboard, mouse, and render threads. The short
  `UiLock` protects model edits and frame snapshots; GPU drawing and action
  callbacks run after unlocking. Keep capture/visibility state atomic because
  the client callbacks read it outside that lock. Change cursor capture only
  in the frame callback. Module getters must be quick, side-effect-free reads.
- Key codes/actions are launcher values: L=76; press=0, repeat=1, release=2.
  Preserve consumed-key release handling and cursor restoration behavior.
- Release simulated input or neutralize the controller when disabled, unfocused,
  or outside gameplay. Preserve physical Ctrl and normal WASD fallback when the
  analog helper is disconnected. Keep rendering callbacks free of blocking IPC
  and USB work; the Python helper owns hardware access.
- The mod targets **Android x86_64**, even though the launcher runs on Linux.
  It uses C++17, `-nostdlib`, no exceptions/RTTI, and no linked libc/C++ runtime.
  Host adapters compile with Linux headers, then resolve host functions through
  `mcpelauncher_host_dlopen` / `mcpelauncher_host_dlsym`. Do not directly link
  ordinary glibc/libstdc++ libraries into the Android mod. Dependencies requiring
  an Android runtime need an appropriate NDK/toolchain design.
- The final mod's only `NEEDED` libraries must be
  `libmcpelauncher_gamewindow.so`, `libmcpelauncher_menu.so`, and
  `libmcpelauncher_mod.so`. The launcher supplies them at runtime.
- Recheck native handle layout and vtable slots before changing launcher support
  in `native_cursor.h`. Preserve all GL state touched by rendering modules and
  reset history when gameplay, dimensions, or contexts change.
- AutoGG, Zoom, and Tablist read their game-specific build ID, offsets, object layouts,
  and instruction signatures from `minecraft_build.h`. The current profile
  supports Minecraft 1.26.52.3 Android x86_64. The code currently compiles one
  profile at a time. Re-derive and validate a new binary before changing that
  profile; never loosen a build-ID or signature gate to make it load. The sender
  accepts
  the verified ClientNetworkHandler and LegacyClientNetworkHandler vtables; the
  legacy constructor calls the base constructor without a this-pointer
  adjustment, preserving the client at +0x58 and TextPacket handler at vtable
  +0x120. Preserve the original dispatcher and fail without patching on an
  incompatible build/slot.
  `native_input.h` additionally assumes the launcher's libstdc++ callback ABI.
- Minecraft code/dispatcher writes go through `hooks::install()` in
  `hook_manager.cpp`. Initialize with `hooks::initialize()`, find the supported
  binary with `hooks::find_game()`, and validate module-specific instructions
  and ABI getters with `hooks::matches()` / `hooks::matches_pointer()` before
  constructing a patch batch. Use `hooks::readable()` before any other raw
  profile-based reads, including executable ranges for native function calls.
  Install serially at `mod_init()` before gameplay; the batch is not an atomic
  transaction across multiple sites. All original bytes and page permissions
  are checked before writing. The manager reserves up to 64 patches, rejects
  duplicate owner names and overlapping ranges, and restores original page
  permissions rather than assuming all targets are RX or read-only.
  Owners must be stable string literals; installed hooks live until process exit.
  `Rejected` means no writes, `RolledBack` means all originals/permissions were
  restored, and `Retained` means rollback was incomplete. Keep feature behavior
  inactive and keep any referenced relay alive on `Retained`. Do not free a
  successful hook's relay or add runtime unhooking without a thread/lifetime design.
  Use `hooks::allocate_near()` and `hooks::make_executable()` for relays, keeping
  code RX and data on a separate RW page. Module-specific relay instructions
  and native object behavior remain in the modules. Reuse an existing hook's
  owner through a typed callback/service when multiple modules need the same
  interception point; never install two owners onto the same address.
- Zoom's four camera reads are recorded in the selected build profile and call
  the FOV option getter. Preserve their build/signature gate and re-derive
  callers before supporting other builds.
  A nearby executable relay redirects only these read-only camera sites;
  per-thread proxies resolve native overrides (+8 -> +0x1a0), scale float
  min/max/value (+0x10/+0x14/+0x18), and never modify native option storage.
  The native minimum-FOV load at `0xcd0aa33` is also signature gated and
  redirected to a separate writable relay data page: 5 degrees normally,
  0.1 degrees during zoom, so high magnification is not flattened by the native
  floor. Keep executable relay code on its separate RX page.
  The first call crosses a cache line: patch its preceding getter/id setup
  at `0xaba3ef7` with a jump to a separate relay entry, then resume at
  `0xaba3f05`. All eight-byte patches stay inside one cache line.
  Install at mod_init before gameplay; retain original results when inactive.
- Keep `-ffreestanding` for the Android code and AutoGG host adapter; `runtime.cpp`
  provides hidden `memset`/`memcpy` to avoid introducing a libc dependency.
  AutoGG configuration edits are copied under a short lock, then saved atomically
  from the frame callback outside the UI lock. Do not overwrite
  `odiclient.conf` or legacy `autogg.conf` during installation or ship personal
  settings in a distribution archive.
- Analog IPC uses an abstract per-user `SOCK_SEQPACKET` socket named
  `nuphy-analog-<uid>`, same-UID peer checks, and 8-byte `NAC1`/`NAS1` packets.
  Update both sides and tests together if the protocol changes.

## Updating Minecraft version support

Render also uses the exact profile in `minecraft_build.h`: a LevelBuilder
render-list callback vtable slot, closure captures, selected output vector and
terrain-list consumer signatures. Re-derive all of them for a new binary. Its
`Render` hook owner uses the shared manager and preserves the native callback,
including when inactive or installation is retained. Filter only the appended
suffix before native camera-list merging; never skip dirty-section bookkeeping
or filter the final list after corresponding visibility masks have been built.

When a Minecraft update breaks a native feature, confirm the game binary first:

1. Find it at `~/.local/share/mcpelauncher/versions/<version>/lib/x86_64/libminecraftpe.so`.
   Check its architecture and GNU build ID with `file` and
   `readelf -n <binary>`. Do not infer the build ID from the version folder.
2. Re-derive every AutoGG and Zoom address, object layout, vtable slot, and
   instruction signature used by the feature. A matching-looking old offset is
   not evidence that its meaning stayed the same. If the new layout or call path
   is unclear, keep that hook unsupported.
3. Put verified values in `minecraft_build.h`; keep version-specific addresses
   and signatures out of the module implementations. Both hooks must continue to
   check the exact GNU build ID and expected vtable/code signatures before patching.
4. Run `python3 tests/test_native_build.py <binary>`. It checks the selected profile
   against the candidate, including AutoGG's dispatcher, vtables, sender call
   sites, and Zoom's camera reads and FOV floor. A failure means the binary and
   profile do not match. The checker validates supplied values; it cannot locate
   safe hooks, identify changed C++ layouts, or prove chat sending works in game.
5. Update native fixtures in `tests/test_auto_gg.cpp` and `tests/test_zoom.cpp` when their
   gates or call-site patch shapes change. Run the shared manager checks too,
   then rebuild with `bash build.sh`. A new build normally changes
   `minecraft_build.h` and the feature's ABI bridge, not the shared patch manager.
   Update the supported-version text in this file and `README.md` only after
   the profile and checks agree.

This workflow validates one selected game build; it does not add simultaneous
support for older versions. Keep an unsupported build fail-closed until its
native behavior has been re-derived and checked.

## Building and checking

Build dependencies on Arch: Clang, binutils, libx11, libglvnd, and FreeType
headers (`freetype2`), curl and json-c headers (`curl`, `json-c`). The mod loads the host FreeType runtime dynamically.
Host tests also need g++; graphics checks need Mesa's software renderer. From
the project root:

```sh
bash build.sh
```

This updates `build/`, `1.0.0/x86_64/`, and installs into the launcher's mods
directory. To stage elsewhere, use `bash build.sh /tmp/blank-client-menu-mods`.
`bash install.sh [mods-directory]` installs an existing binary and copies root
helpers/README. Restart the launcher and activate the mod in the chosen profile;
restart Minecraft after rebuilding to load the new binary. Build prints source/install paths, build ID and binary SHA-256; `build-info.txt` records that identity. See `docs/CODE_QUALITY.md` for the scoped architecture review.

Run the checks relevant to the changed module (create `build/` first if absent):

```sh
mkdir -p build
g++ -std=c++17 -Wall -Wextra -Werror tests/test_fps_display.cpp display_layout.cpp -o build/test-fps-display
./build/test-fps-display
g++ -std=c++17 -Wall -Wextra -Werror tests/test_ui_animation.cpp -o build/test-ui-animation
./build/test-ui-animation
clang++ -std=c++17 -Wall -Wextra -Werror -I/usr/include/freetype2 \
    tests/test_popup_render.cpp popup.cpp custom_font.cpp motion_blur.cpp -ldl -lEGL -lGLESv2 -o build/test-popup-render
LIBGL_ALWAYS_SOFTWARE=1 MESA_SHADER_CACHE_DIR=/tmp/mcpelauncher-mesa-cache ./build/test-popup-render
g++ -std=c++17 -Wall -Wextra -Werror client.cpp autosprint.cpp tests/test.cpp -o build/test-menu
./build/test-menu
g++ -std=c++17 -Wall -Wextra -Werror tests/test_custom_menu.cpp motion_blur.cpp \
    -ldl -lEGL -lGLESv2 -o build/test-custom-menu
LIBGL_ALWAYS_SOFTWARE=1 MESA_SHADER_CACHE_DIR=/tmp/mcpelauncher-mesa-cache ./build/test-custom-menu
LIBGL_ALWAYS_SOFTWARE=1 MESA_SHADER_CACHE_DIR=/tmp/mcpelauncher-mesa-cache ./build/test-custom-menu --anchors
g++ -std=c++17 -Wall -Wextra -Werror tests/test_sdl_input.cpp -o build/test-sdl-input
./build/test-sdl-input
python3 tests/test_native_build.py
g++ -std=c++17 -Wall -Wextra -Werror tests/test_environment.cpp hook_manager.cpp -ldl -o build/test-environment
./build/test-environment
g++ -std=c++17 -Wall -Wextra -Werror tests/test_sky_renderer.cpp -ldl -pthread -lEGL -lGLESv2 -o build/test-sky-renderer
LIBGL_ALWAYS_SOFTWARE=1 MESA_SHADER_CACHE_DIR=/tmp/mcpelauncher-mesa-cache ./build/test-sky-renderer
# Optional 768x512 day/night PPM previews in build/: add --preview.
g++ -std=c++17 -Wall -Wextra -Werror tests/test_particles.cpp hook_manager.cpp -ldl -o build/test-particles
./build/test-particles
g++ -std=c++17 -Wall -Wextra -Werror tests/test_hook_manager.cpp -ldl -o build/test-hook-manager
./build/test-hook-manager
g++ -std=c++17 -Wall -Wextra -Werror tests/test_zoom.cpp hook_manager.cpp -ldl -pthread -o build/test-zoom
./build/test-zoom
g++ -std=c++17 -Wall -Wextra -Werror tests/test_player_target.cpp -o build/test-player-target
./build/test-player-target
g++ -std=c++17 -Wall -Wextra -Werror tests/test_chat.cpp chat.cpp -o build/test-chat
./build/test-chat
g++ -std=c++17 -Wall -Wextra -Werror tests/test_auto_gg.cpp chat.cpp hook_manager.cpp -ldl -o build/test-auto-gg
./build/test-auto-gg
# Settings/behavior checks without the native hook fixture:
./build/test-auto-gg --settings
g++ -std=c++17 -Wall -Wextra -Werror -DTABLIST_PRESENCE_FIXTURE \
    tests/test_tablist.cpp tests/test_flarial_presence.cpp skin_image.cpp hook_manager.cpp -ldl -pthread -o build/test-tablist
./build/test-tablist
g++ -std=c++17 -Wall -Wextra -Werror tests/test_skin_image.cpp -ldl -pthread -o build/test-skin-image
./build/test-skin-image
g++ -std=c++17 -Wall -Wextra -Werror tests/test_flarial_presence.cpp -ldl -o build/test-flarial-presence
./build/test-flarial-presence
g++ -std=c++17 -Wall -Wextra -Werror tests/test_render.cpp render_frame_trace.cpp render_gl_trace.cpp hook_manager.cpp -ldl -o build/test-render
./build/test-render
g++ -std=c++17 -Wall -Wextra -Werror tests/test_render_gl_trace.cpp hook_manager.cpp -ldl -o build/test-render-gl-trace
./build/test-render-gl-trace
g++ -std=c++17 -O2 -Wall -Wextra -Werror tests/test_render_frame_trace.cpp hook_manager.cpp -ldl -o build/test-render-frame-trace
./build/test-render-frame-trace
./build/test-render-frame-trace --present-only
./build/test-render-frame-trace --submit-only
g++ -std=c++17 -Wall -Wextra -Werror tests/test_gpu_shader_services.cpp hook_manager.cpp -ldl -o build/test-gpu-shader-services
./build/test-gpu-shader-services
bash tests/test_build_install.sh
python3 tests/test_analog.py
g++ -std=c++17 -Wall -Wextra -Werror analog_input.cpp tests/test_analog_input.cpp -ldl -o build/test-analog-input
./build/test-analog-input
g++ -std=c++17 -Wall -Wextra -Werror motion_blur.cpp tests/test_motion_blur.cpp -ldl -lEGL -lGLESv2 -o build/test-motion-blur
LIBGL_ALWAYS_SOFTWARE=1 MESA_SHADER_CACHE_DIR=/tmp/mcpelauncher-mesa-cache ./build/test-motion-blur
ln -sfn ../assets build/assets
clang++ -std=c++17 -Wall -Wextra -Werror -I/usr/include/freetype2 tests/test_custom_font.cpp \
    -ldl -lEGL -lGLESv2 -o build/test-custom-font
LIBGL_ALWAYS_SOFTWARE=1 MESA_SHADER_CACHE_DIR=/tmp/mcpelauncher-mesa-cache ./build/test-custom-font
readelf -d build/libblank-client-menu.so
```

After C++ changes, also build the Android mod and check its dependencies. Mock
tests cannot verify in-game appearance, FPS, actual keyboard hardware, or native
Wayland compatibility; report which checks were actually run.

## Adding a simple module

Follow the existing `.cpp`/`.h` pattern when a feature needs separate logic. Wire
its lifecycle into `client.cpp` and declare its typed controls in
`client_modules.h`. Add its tile and settings page in `declare_menu_pages()` in
`menu_pages.cpp`, using `newTile()`/`newPage()` and the existing control builders.
The menu supports 36 tiles, 36 pages, and 16 controls per page, displayed as up to
nine visible tiles and five settings rows, both with mouse-wheel scrolling. Registration
happens once when the menu first opens, not while rendering; labels and asset
paths must remain valid for the lifetime of the menu. Builder overflow or invalid
slider ranges are reported by `custom_menu_build_error()` and the footer.
Call `.whenEnabled()` immediately after a dependent control to place it inside
its preceding root toggle group. Dependents must be contiguous with their root
toggle; dependent toggles share the root group and do not create nested groups. Use `toggle()` for boolean settings; `choice()` selects between two
options using a boolean setter/getter; `whenEnabled(predicate)` also filters a
dependent by mode. Hidden controls keep their values,
take no layout space, and cannot receive input. Layout evaluates getters outside
`UiLock`, scrolls visible controls without repeating toggles, and clamps the scroll position after collapse. Mouse hit
tests use the last rendered control layout; hidden text focus/slider drag is
cleared when that layout changes.
A togglable tile needs both its setter and getter; non-togglable tiles keep a
neutral appearance. Getters should read module state without side effects.
Action callbacks may safely call `custom_menu_back_to_tiles()` after unlocking.
Add a new source to `host_sources` in `build.sh` if needed; keep Android/runtime
sources in the target compilation. The font renderer caches up to 36 icon paths.
Extend persisted settings through `client_settings.h` and the existing
`odiclient.conf` backend in `auto_gg.cpp`, with migration tests, without altering
AutoGG's verified native bridge just to add a setting. Reuse launcher APIs and
existing host-loading patterns before
adding dependencies or hooking Minecraft internals. If a native hook is needed,
keep its verified profile values in `minecraft_build.h` and use the shared hook
manager; do not copy game discovery, `mprotect`, or rollback into a new module.
Add relevant behavior checks,
update `README.md`, and adjust package metadata when the feature list changes.
Avoid unrelated refactors and speculative infrastructure for future modules.
