# ODIclient for MCPELauncher

## Install and distribute on Arch Linux

This folder contains the compiled x86_64 mod, its source, and the optional NuPhy
keyboard helper. Install MCPELauncher and your own Minecraft game files first.
The mod uses native launcher 1.8.4 APIs and supports SDL3/Wayland and EGLUT/X11 (including XWayland);
compatibility with other launcher versions or window backends is unverified.

AutoGG, Zoom, Render, Tablist, and Particles use the exact Minecraft build profile in `minecraft_build.h`.
To check a candidate game binary against the current profile, run
`python3 test_native_build.py /path/to/libminecraftpe.so`. This verifies the
build ID, vtables, and expected code signatures. Finding safe addresses and
confirming changed native layouts still requires inspecting each new game build.

Extract `ODIclient-arch-x86_64.tar.gz`, then run:

```sh
cd ODIClient
bash install.sh
```

The installer copies the runtime files into
`${XDG_DATA_HOME:-$HOME/.local/share}/mcpelauncher/mods/ODIClient/1.0.0/x86_64/`
with a `mod.json` file so it appears in the launcher's installed-mod list.
Restart the launcher, open **Mods → Installed Mods → ODIClient**, and
click **Activate** for version **1.0.0** in your chosen profile. Launch Minecraft
and press **L**. Use **Disable** there to turn off the mod for that profile.
For a custom launcher data directory, pass its mods
directory as the first argument: `bash install.sh /path/to/mods`.

To create a fresh shareable archive after building, run from this folder:

```sh
tar --exclude=ODIClient/build --exclude=ODIClient/skins --exclude=__pycache__ \
    --exclude=odiclient.conf --exclude=odiclient.conf.tmp \
    --exclude=autogg.conf --exclude=autogg.conf.tmp \
    -czf ../ODIclient-arch-x86_64.tar.gz -C .. ODIClient
```

Share that archive. It includes the installer, binary, source, and helper;
Minecraft files and other installed mods are not included. Recipients do not
need a compiler unless they want to rebuild.

This registers the mod locally under **Installed Mods**. Appearing in the public
**Mods** download catalogue requires hosting a release and submitting an entry
to https://github.com/minecraft-linux/mcpelauncher-moddb. No release is published
by this installer.

Restart Minecraft and press **L** during gameplay to open the **Custom Menu**
panel with a frosted-glass `#1c1920` background. It blurs only the panel area and applies a
stronger tint for a less transparent look. It uses a centered landscape 4:3 aspect ratio
and uses a 480-by-360 logical panel, choosing the largest common scale that fits
within half the screen width and height; very small windows use a fitted scale.
Text uses FreeType hinting at its actual integer pixel height, with a bounded
16-atlas cache (at most 6 MiB of R8 texture data). The settings zoom animation
reuses settled-size glyphs while scaling them; description rows keep a stable
glyph atlas, scale continuously, and share the toggle labels' left padding. Text becomes pixel-aligned when
settled. Font heights above 60 pixels use scaled 60-pixel glyphs. The panel is not draggable.
While open, the layout updates after a screen dimension changes by at least 5%.
Left-clicking the Tablist, Zoom, Auto Sprint, Motion Blur, FPS Limiter, Render, Analog WASD, AutoGG, or Chat mods
tile toggles that module and updates its color; right-clicking a tile opens its
page. AutoGG text boxes edit the saved trigger and response and accept
Shift-modified letters and punctuation such as `!`. Enable Zoom, then hold **C**
in gameplay and scroll up/down to adjust magnification from 1.5x to 30x.
The **Default zoom** slider chooses the starting level for each hold
(1.5x–30x; 3x by default). Scroll changes last until you release the key.
**Scroll step** controls sensitivity from 0.1x to 5x per wheel step
(default 0.5x: 3x becomes 3.5x). Scrolling stops at either limit, and one step
in the opposite direction changes zoom immediately.
In Zoom settings, click **Hold to zoom** and press a new key; Escape cancels.
L and Escape remain reserved for menu navigation. Zoom defaults OFF, and its
key cannot enable a disabled module. Its enabled state, binding, default level, and scroll sensitivity persist in
`odiclient.conf`. Zoom narrows the world camera FOV without magnifying the HUD
or changing Minecraft's saved FOV. The native camera hook supports only the
verified Minecraft 1.26.52.3 Android x86_64 build; incompatible builds leave
zoom unavailable and show the reason on its settings page. Zoom ends
on key release, disable, focus loss, or leaving gameplay/opening the client menu. Text buttons and fields use the
original plum tint at 15% opacity without an extra blur pass. Right-clicking
blank panel space or pressing Escape returns to the tiles; Escape from the tile
page or **L** closes the menu. Tile buttons, page buttons, and text boxes have
antialiased rounded corners; the blurred panel and its crimson border are rounded too.
The menu uses consistent padding, centered toggle labels, a visible text-field focus
outline, and a header/footer. Entering settings fades contents in over 220 ms while they zoom from 75% to 100% around the panel center using ease-out quart. Leaving settings uses ease-in quart to shrink those contents from 100% to 75% and fade them out; the module grid starts fading in after 110 ms. The outer panel stays fixed during settings transitions. The base title keeps its size and opacity; `ODIClient` slides left over 220 ms while ` - <page>` fades in, forming a centered title. On exit the suffix disappears and the base title slides back to center. Scrollbar and slider colors, opacities, and corner radii are set in `menu_style.h`, alongside Tablist colors, opacities, background tint, blur strength, and corner radii. Tablist uses the same quarter-resolution background blur refreshed at most 30 Hz. Its settings can choose Inter or the Mojangles font from the latest installed launcher version; Inter is used when Mojangles is unavailable. The entire panel background is blurred at quarter resolution, refreshed at most 30 times per second, with a 70% dark tint, with the unblurred title bar drawn on top. The color change separates the sections without a divider line. The module grid and settings panels scroll with the mouse wheel when needed, using a 180 ms ease-out quart animation with clipped content and fixed headers/footers.
Enable toggles expand their bubble to show dependent settings: Motion Blur
mode and its Strength or Target Hz, FPS Limit, and AutoGG trigger/response.
Each implemented module has an Enable toggle on its settings page.
Turning a toggle off hides those controls without changing their saved values;
following controls move up to fill the space.
The menu fades and slides with a 500 ms exponential ease-out animation. It captures
keyboard and mouse input halfway through opening (250 ms) and releases input as soon as
closing starts; cursor capture follows on the next frame. Page controls
include a clickable text-only button and an editable textbox. The panel and tiles are drawn by the mod, so their
colors can be changed independently of the launcher's menu theme. Rebuild and
restart Minecraft to load updates.

### Particles

Enable **Particles** from the L menu to show a burst of native critical-hit
particles when you left-click a player within Minecraft's normal attack reach.
It defaults OFF; the toggle persists in `odiclient.conf`. The effect is local and
cosmetic: it does not change damage or make the attack a critical hit. Mobs,
blocks, and empty-air swings do not trigger it. It follows native attack attempts,
including attempts the server may reject, rather than waiting for a damage event.

The exact-build hook uses Minecraft's selected attack target and native emitter;
no player-location polling or camera ray approximation is needed. It forwards
all original attack arguments and return values, keeps native objects only during
the callback, and disables effects outside focused gameplay or while the custom
menu is visible. Unsupported builds leave the hook inactive. This implementation
supports Minecraft 1.26.52.3 Android x86_64; appearance needs in-game verification.
See [particles_research.md](particles_research.md) for the native evidence.

### Player tablist

**Hold Tab** during gameplay to see the server's player roster, including players
outside render distance. Tablist defaults ON; toggle it from its tile or settings
page in the L menu. Its enabled state persists in `odiclient.conf`. The passive
overlay keeps mouse capture and camera movement, uses the menu's rounded palette
and 500 ms exponential fade/slide animation, and stays sharp above Motion Blur.
The first wheel step while holding Tab selects the top-left player and opens a
static full-body skin preview, turned 20 degrees. Further steps move down each
column. The visible name columns slide one column at a time as selection moves
beyond them, keeping the last two columns together at the end of a large roster.
A slim horizontal scrollbar uses the menu’s track/thumb styling and 180 ms motion;
it appears when the roster plus preview needs more than three columns. The selected row has a faint white 1-pixel
outline with small rounded corners and no fill. The preview occupies the rightmost
column: it adds a column when space permits or replaces the third name column.
All names remain reachable by scrolling. Column and row size changes expand or
contract over 500 ms. Selection follows the same player through joins, departures, name changes, and
Flarial reordering. If that player leaves, selection moves to the next player, or
the previous one if they were last; an empty roster clears selection. Releasing Tab closes the
overlay and resets selection.
The default roster has no added instructions or preview controls.

While a player is selected, hold **right click for one second** to save their
received skin as `ODIClient/skins/<player>-<timestamp>.png`. A small ring fills
beneath the preview during the hold, followed by a brief save result. Saves retain
the original texture resolution and alpha and run on a worker thread. Releasing
right click early, changing selection, losing focus, or closing Tab cancels the
hold. Right clicks used for saving are consumed, including their releases.
It hides when focus is lost, a Minecraft GUI opens, or the client menu is visible.

Each row shows the player name and a head cropped from the skin received by
Minecraft, including its hat layer. Rows have no background bubble; the list is
compact, starts a new column after at most eight names, and narrows each column to
fit ordinary player names. Standard 64, 128,
and 256 pixel skins (including legacy half-height skins) are supported; unavailable
or unsupported skins use an initial icon. Skin-change packets refresh the heads.
Flarial users appear first, with names sorted within both groups. Formatting
codes are removed and long names shortened to fit.
The shared font currently substitutes `?` for characters outside its ASCII atlas.

Flarial online users sort before other players and show a red `[FL]` prefix.
While the overlay is used, a background
request refreshes Flarial’s public presence list every three minutes; cached badges
expire after six minutes if requests fail. Unsupported names and removal entries
do not discard valid players. Sorting and badge drawing use the same presence
snapshot. Matching follows cleaned names, so
server nicknames and users missing from Flarial’s API may not be detected. This
downloads the online list without sending your name, roster, or server address.
The optional lookup dynamically loads host `libcurl.so.4` and `libjson-c.so.5`
(Arch packages `curl` and `json-c`, also required as build headers); unavailable
libraries leave names without badges. No platform/input-device indicator or
external skin lookup is provided.
World-start/disconnect packets and handler changes reset the roster. The cache
holds up to 4096 players. Full copied skin textures have a separate 64 MiB budget;
the least recently used textures are evicted if an unusually large roster exceeds
it. Names and head icons remain. Unavailable skins show a message only in preview
mode. The preview uses standard player cuboids, including outer skin layers and
legacy mirrored limbs; custom Bedrock geometry and capes are not rendered. Slim
arms are inferred from unused transparent strips in standard skin textures. Servers that
omit roster entries cannot provide a complete list to this overlay.

The native dispatcher and skin layouts are gated to Minecraft **1.26.52.3 Android
x86_64**. Unsupported builds keep Tablist unavailable and show a reason on its
settings page. Restart Minecraft after rebuilding; enter or reconnect to a world
so the roster packets reach the new hooks. Appearance and live multiplayer
rosters need an in-game check.

To declare custom-menu tiles and pages, edit `declare_menu_pages()` in
`menu_pages.cpp`. Module controls are declared in `client_modules.h` and implemented
in `client.cpp`; shared saved-setting calls are declared in `client_settings.h`.
For example:

```cpp
MenuPage page = newPage("Example");
page.text("Page content.");
newTile("Example").opens(page);
```

Add `.onToggle(setEnabled, isEnabled)` for a module tile, using a setter and a
quick getter that read the real module state. The menu supports 36 tiles/pages
and 16 controls per page, with nine visible tiles and five visible settings rows.
The mouse wheel scrolls either panel with a position indicator; builders report capacity errors through
`custom_menu_build_error()` and the footer. Invalid slider ranges are rejected.
Declare menu content once and keep label/asset strings alive for the client's
lifetime. Action callbacks run outside the short menu lock and can return to the
tile view safely. Append `.whenEnabled()` after a control to nest it inside the
preceding toggle's bubble; dependents must immediately follow that toggle, and
nested toggle groups are not supported. For example:

```cpp
page.toggle("FPS-based averaging", client_set_blur_average, client_blur_average)
    .slider("Target Hz", 30, 500, client_set_blur_average_hz, client_blur_average_hz)
    .whenEnabled();
```

`whenEnabled(predicate)` additionally filters a dependent by its mode.
Hidden controls retain their values and do not receive input. Expanded groups scroll as a continuous list; collapsing groups clamps the scroll position. Colors, spacing, and animation timing live in `menu_style.h`.

Page builders support `text()`, `button(label, callback)`,
`choice(label, first, second, setter, getter)` for two modes,
`toggle(label, setValue, getValue)`, `slider(label, min, max, setValue, getValue)`,
and `textBox(label, initialValue, onChange)`. Tiles can bind real module state
with `onToggle(setEnabled, getEnabled)`; icons are optional. Tiles use a 3 by 3
scrolling grid of 3:4 portrait cards with three visible rows, centered in the content area.
A subtle thin divider separates each icon from its label.

Click **Auto Sprint: OFF** to enable the module; the button changes to **ON**.
Close the menu, resume the game, and hold **W** to sprint automatically. It uses
the default bindings: W for forward, Left Ctrl for sprint, and either Shift for
sneak. Set those bindings in Minecraft if you have customized them. Its enabled
state is saved automatically in `odiclient.conf` beside the installed mod. Hunger
and other normal Minecraft sprint rules still apply.

Auto Sprint releases its simulated Ctrl when you stop moving forward, sneak,
open the client menu, leave mouse-locked gameplay, or switch focus away from
Minecraft. It respects physically held Left Ctrl. It targets your installed
launcher's SDL3 backend on Wayland or X11. The SDL adapter reads physical keys
and sends Left Ctrl (launcher key code 17) through the launcher's keyboard callback. EGLUT/X11 and
XWayland retain their X11 input adapter.

Enable **Center Cursor** in the L menu to center the pointer whenever a
Minecraft GUI or the custom menu releases mouse capture. It runs once per
opening, respects focus, and uses window coordinates for HiDPI screens. The
setting defaults OFF and is saved in `odiclient.conf`.

Click **Motion Blur: OFF** to enable frame blending, including while the client
menu is open. **Blur strength (%)** controls how much of the previous blended
frame remains: 0 disables the effect, 30 is the default, and 80 is the maximum.
Higher values give longer trails. This is frame blending, so strength depends
on frame rate and can create ghosting. Its enabled state and strength are saved
automatically in `odiclient.conf` beside the installed mod.
Open **Motion Blur settings** and use **Enable Motion Blur** as the master switch.
Inside it, choose **Trail** to show Strength or **FPS average** to show Target Hz.
FPS average works even when the saved Trail strength is zero. Screen blur has
its own independent switch. Existing averaging settings select the matching mode.

**FPS-based frame average** timestamps captured frames and weights them by how
much of the selected 30–500 Hz time window they cover. At steady 90 FPS with a
60 Hz target, for example, it blends 2/3 of the current frame with 1/3 of the
previous frame. It pauses Motion Blur when a frame interval exceeds the target
window, and keeps at most sixteen frame textures. It defaults off. Its enabled
state and target Hz are saved in `odiclient.conf` and restored after restart.

The effect runs on the GPU before the client menu is drawn. Minecraft's own HUD
and hand are included; the client menu remains sharp and does not reset history.
Leaving mouse-locked gameplay (including opening Minecraft screens), resizing the window, switching graphics contexts, or
disabling the effect resets its frame history. It needs OpenGL ES 3 or desktop
OpenGL 3.3, plus an EGL context. It stores up to sixteen full-resolution RGBA
textures (about 127 MiB at 1080p or 506 MiB at 4K at the maximum). More
frames increase GPU texture reads and can lower FPS. It allocates history as the selected frame count requires; the
experimental average copies a raw frame, and active modes draw across the
screen each frame. Measure FPS in your
world before choosing a strength; the strength setting does not reduce that cost.

**Experimental screen blur** applies a fixed spatial blur to the whole game
screen, including inventory panels, items, text, and the HUD. It also runs during
gameplay and other Minecraft menus; the custom menu stays sharp so you can turn
it off. This test effect defaults OFF and its setting is saved in
`odiclient.conf`. It shares Motion
Blur's EGL/GLES 3 or OpenGL 3.3 requirements and adds a full-screen GPU pass.
It does not mask the inventory or make its background translucent.

**FPS Limit** adds an optional 30–480 FPS cap while Minecraft is rendering,
including inventory screens and the custom menu. It only
slows frames when they are arriving faster than the selected limit; Minecraft's
own FPS setting and VSync can impose a lower cap. To allow rates above 60, set
Minecraft's FPS option to **Max** and turn VSync off. Actual performance still
depends on the display, launcher, and hardware.

## Experimental Render limits

Open **L → Render**, enable **Enable Render**, then choose **Cull below camera**
or **Cull above camera**. Both choices and the master switch default OFF.
Their distance sliders accept 16–256 blocks, with defaults of 64 below and
128 above. Settings persist in `odiclient.conf`; older configurations migrate
with Render disabled. Existing vertical settings are preserved when upgrading.

The module removes whole 16-block terrain sections outside the selected vertical
range before Bedrock looks up their meshes and prepares its render list. Sections
partly inside the range remain. The bounds follow the camera, including underground.
Native frustum checks, dirty-section bookkeeping, and multiple-camera list merging
remain active. World loading and simulation continue normally.

**These limits can hide visible terrain**, including cliffs, cave openings, and
terrain seen while looking down from a height. They do not test whether terrain
is hidden behind other blocks. Start with the below-camera default and compare
the same scene with the switch off/on. A smaller limit removes more terrain.
The exact Minecraft 1.26.52.3 x86_64 build is required; unsupported builds show a
reason on the settings page and retain native behavior.

The owner reports about 6% higher FPS with aggressive vertical distances.
Bedrock already performs frustum
culling, and this hook adds a scan over the sections that survive it. The benefit
depends on how much downstream terrain work is removed. Compare frame times in
the same world, position, camera direction, resolution, and render distance, with
FPS Limit, VSync, and blur off. At 600 FPS a frame is about 1.67 ms; reaching
1,000 FPS requires bringing it down to 1 ms. See [render_research.md](render_research.md)
for the native evidence and remaining investigation targets.

## Chat mods

Open **Chat mods** in the L menu, enable the module, and toggle **Message blacklist**.
The **Keywords** text box appears when the blacklist toggle is on. Press
**Shift+Enter** to insert a new line; commas still separate keywords. Enter keywords
separated by commas, for example `spam, buy now`. Incoming messages containing
any keyword are hidden. Matching ignores ASCII letter case and Minecraft color
codes, trims spaces and newlines around keywords, and skips empty entries. Other UTF-8 text
matches literally. An empty list hides nothing. Both toggles default OFF; toggles
and the list (up to 255 UTF-8 bytes) persist in `odiclient.conf`.
Filtering shares AutoGG's exact-build incoming TextPacket hook, so it requires
Minecraft 1.26.52.3 Android x86_64. AutoGG can still respond to hidden messages.
Local chat echoes and unsupported packet payloads are not filtered.

## AutoGG

Press **L** and enable **AutoGG**, then open **AutoGG settings** to edit the text.
**AutoGG trigger** defaults to
`You won the game`; **AutoGG response** defaults to `gg`. Both fields are editable
in the menu. The trigger is a case-sensitive substring of incoming chat, with
Minecraft color/format codes removed. For example, a colored
`Congratulations! You won the game!` matches the default. Command messages
such as `/tell @a Hi` and `/say Hi`, and player chat, can also trigger it.
This is intended for CubeCraft's win message; it is not
restricted to a particular server, so disable it or change the trigger elsewhere.
Use **Back to modules**, Escape, or X to leave the settings view. While editing,
L types the letter normally; the main modules view keeps the L close shortcut.
The blacklist field starts one line tall and expands as newlines are inserted.
Text boxes support Shift for uppercase and symbols, Caps Lock, and held-key
repeats. A crimson caret blinks while a field is focused and restarts its blink
after edits. It glides to its new position with a 160 ms exponential ease-out
animation while typing or deleting. Enter ends editing; Shift+Enter inserts a new line in fields that
enable multiline input, currently blacklist Keywords. Long fields show the
editing end, and multiline fields show the latest lines.

The trigger, response, Lobby Scanner toggle and rules, AutoGG, Auto Sprint, Motion Blur, FPS-based averaging,
target Hz, experimental screen blur, Render limits, and FPS Limit settings are saved
automatically in `odiclient.conf` beside the installed `.so` in `1.0.0/x86_64/`.
The FPS limit value is saved too (default 120). Existing `autogg.conf` settings
are loaded and migrated automatically when `odiclient.conf` is absent. On a fresh
install, module toggles default to OFF and blur strength defaults to 30%.
Rebuilding/reinstalling keeps that file. Text must be one line, at most 255 UTF-8
bytes. Blank trigger/response prevents sending; slash commands are rejected.
Duplicate matches within 15 seconds are ignored, and a response containing its
own trigger is suppressed to prevent loops.

AutoGG sends a normal chat packet through Minecraft's native sender without opening
chat, typing keys, or depending on the T binding. It uses the current player's name,
XUID, and platform identity. AutoGG keeps working while the client menu is open.
A match received while Minecraft menus/chat are open or the game is unfocused is dropped. Keyboard presses suppress sending until the next frame.
Local worlds also display the response through Minecraft's normal local chat path;
remote servers provide their own chat echo. AutoGG settings report when a response
is sent, a duplicate is ignored, or a match is skipped because a menu, focus, or
keyboard-input check blocked it. If the live player or sender is unavailable, the
settings show an error; failed sends do not consume the 15-second cooldown.

Chat detection currently supports **Minecraft 1.26.52.3 Android x86_64**, build ID
`3aae9851841480362ffb2b0aabecda2a60b29b27`, with launcher **1.8.4** using the host
libstdc++ ABI. Other game builds show **AutoGG: unsupported Minecraft build** and
leave the packet dispatcher untouched. Native Wayland uses SDL3 physical-key and focus tracking. Automated tests cover matching,
native sender bridging, cancellation, and saving; actual CubeCraft sending still needs an in-game check.

## Lobby Scanner

Enable **Lobby Scanner** from the L menu and enter comma-separated
`player/command` rules, for example `steve/hub,alex/home`. When a matching
player is added to Minecraft's roster, `player/command` sends a command request
once for that join. Use `player#/message` to send the message literally in
public chat, including a leading slash, as in `steve#/hub`. Player names match
without regard to ASCII case, and a rule can use any part of a name. Add `?`
after the name fragment to ask first: `steve?/hub` prompts before running `/hub`,
and `steve?#/hub` prompts before sending the literal `/hub` in public chat.
The popup identifies the player and shows the action. Hold left click for one
second for Yes, or right click for one second for No. No, cancellation, or the
10-second timeout skips the action. The popup works independently of Experimental.

Confirmed actions send once on the next observed native chat, roster, skin,
time, movement, or network-latency dispatch while gameplay is focused; the mod
retains only copied action text and a numeric session identity. Disabling Lobby
Scanner, editing its rules, or a world/handler change cancels pending actions.
One confirmation can be pending at a time; if another prompt is busy, the new
matching join is skipped. Rules without `?` still run immediately. The toggle
and rules persist in `odiclient.conf`.

## Analog WASD (NuPhy Air60 HE)

`nuphy_analog.py` imports the bundled `nuphy_distance.py` and feeds its
WASD travel readings into a virtual Xbox 360 left stick. Keep both files in
this folder. It uses Python's standard library and the kernel's `/dev/uinput`;
no extra Python packages are needed. Stop any other keyboard travel reader first.

On the original machine the vendor interface was `/dev/hidraw4`. Find your
interface with `python3 nuphy_distance.py --list`, then give your normal user access
in a terminal (the number can change after reconnecting the keyboard):

```sh
sudo setfacl -m u:"$(id -un)":rw /dev/hidraw4
python3 nuphy_analog.py --check
python3 nuphy_analog.py
```

Run those commands from this directory. The ACL lasts until the device is
recreated. Your user already has access to `/dev/uinput`. On another machine,
if it is missing, run `sudo modprobe uinput`; if access is denied, grant your
user access to that device as well. The launcher also needs read access to
the generated `/dev/input/event*` controller node; the helper checks this.
Run the helper as the same normal user as Minecraft: its local connection
checks user identity, so running it with `sudo` will not connect to your mod.

Leave the helper running, restart Minecraft to load the mod, then enable
**Analog WASD** by left-clicking its tile in the **L** menu. Right-click opens
the tile page; left-click toggles it. Close
the menu and test in a world. Light
presses walk slowly; full presses request full stick movement. The default
deadzone is 0.1 mm and full travel is 3.3 mm. Calibrate if necessary:

```sh
python3 nuphy_analog.py --deadzone-mm 0.1 --full-mm 3.3
```

Minecraft may apply its own controller deadzone or movement curve. Opposite
keys cancel and diagonals are limited to the same maximum stick magnitude.
If Auto Sprint is also enabled, sprint is requested only near full forward
travel (90%); vanilla sprint and sneak rules still apply.

The mod enables the launcher's mixed keyboard/controller input for this
process at startup (`MCPELAUNCHER_CLIENT_RAW_INPUT=1`). Normal mouse aiming and
keyboard actions remain available; controller button prompts may appear.
While Analog WASD is connected and enabled in gameplay, ordinary WASD presses
are consumed to avoid full-speed keyboard movement. If the helper is absent,
normal keyboard movement remains available. Opening the client menu, leaving
gameplay, losing focus, or disabling the toggle neutralizes the stick.
The helper also neutralizes after 0.5 seconds without mod commands. Keyboard
reports describe changes: a held key keeps its last depth until a release,
and quiet reports do not time out movement. A USB disconnect exits the helper
and removes its controller. Small 0.01–0.02 mm fluctuations are ignored;
movement updates are coalesced to at most 60 per second and send only changed
axes. Menu/focus/disconnect neutralization bypasses that rate limit.
The toggle starts OFF each launch. **Ctrl+C** stops the helper, removes its
virtual controller, and restores keyboard reporting settings.

The launcher stores window position and size in its data directory's `imgui.ini`.
Its initial
empty-window size can be tiny. For this installation, a 480-by-300 layout at
position 120,90 was prepared, centered for the launcher's default 720-by-480
game window. This is a saved layout, not automatic centering at every resolution.
Drag the title bar to move it and the bottom-right corner to resize it; the
launcher remembers those changes. Edit `imgui.ini` only while Minecraft is closed.

The installed binary is `1.0.0/x86_64/libblank-client-menu.so` relative to the
mod's root folder. The launcher loads it when activated for your profile.
The installer removes the top-level loader symlink from the earlier package;
activate this version in **Installed Mods** before playing again.

## Build your own

Module lifecycle and controls live in `client.cpp`/`client_modules.h`; menu declarations
live in `menu_pages.cpp`. After editing root sources, run from this directory:

```sh
bash build.sh
```

The build places the binary in `1.0.0/x86_64/` and runs the installer. Pass a custom
mods directory to `build.sh` to install elsewhere.

On Arch, install the build dependencies and host runtime libraries with
`sudo pacman -S clang binutils libx11 libglvnd freetype2 curl json-c`.

`mod_preinit()` registers a window-creation callback. Once the game's window
exists, that callback registers keyboard, mouse, and per-frame callbacks.
`custom_menu.cpp` owns the menu model, input, layout/animation, and rendering;
`menu_pages.cpp` declares pages and tiles with the builder API. Model edits and
frame snapshots use a short lock; rendering and action callbacks run after it is
released. `custom_menu_is_visible()` includes both animations, while
`custom_menu_captures_input()` controls input capture and gameplay coordination.
The menu calls `draw_gl_panel()` from `panel_renderer.h`, with named `PanelPaint`
properties for tint, blur, corner radius, opacity, and outline width. Its shared
GL adapter lives in `motion_blur.cpp`; rounded borders use shader pixel coverage
without requiring MSAA. `custom_font.cpp` loads the bundled
`assets/inter.ttf` through the host FreeType library and draws tile and page text.
Text and icons disable inherited game clipping tests while drawing and restore
the game's GL state afterward; texture uploads also preserve pixel-unpack settings.
It also loads tile icons for Zoom, Motion Blur, FPS Limiter, Particles,
Center Cursor, and AutoGG from `assets/icon-*.png`. The build and installer
copy all matching icons beside the binary. The panel renderer recalculates centered bounds only
when the drawable changes by at least 5%.
`autosprint.cpp` reads physical keys through SDL3 or X11 and sends sprint through the launcher callback or X11 respectively.
`analog_input.cpp` exchanges enable/focus/sprint flags and sensor status over a
nonblocking Unix socket with `nuphy_analog.py`. The helper owns the USB reader
and virtual controller; no USB work or blocking IPC runs on the render thread.
`motion_blur.cpp` resolves host EGL/GL functions, blends the saved frame over
the current back buffer, and captures the result before the launcher draws its
menu. The Motion Blur settings view contains strength, FPS-based averaging,
and target Hz controls. FPS-based averaging weights timestamped
frames by their overlap with the selected time window, up to 16 frame textures.
It pauses temporal blur when frame time exceeds the target interval. These
settings default off and reset when the mod restarts. It restores the GL state
that it changes so Minecraft can render normally.
`native_cursor.h` bridges the native GameWindow cursor methods in launcher
1.8.4, because the C mod API lacks a cursor setter. Recheck that native vtable
and GameWindowHandle layout before using this cursor feature with a launcher
that changes its native ABI. Cursor changes run outside the UI lock to avoid
deadlocks when the menu closes.
`hook_manager.cpp` provides the shared native-hook plumbing for AutoGG, Zoom, and Render:
game discovery, exact build-ID and mapped-memory checks, named patch ownership,
eight-byte patch batches, page protection restoration, rollback, and nearby relay
allocation. It rejects overlapping hooks and validates every expected word before
writing any patch in a batch. Hooks install before gameplay and stay installed
until process exit; toggling a module changes its behavior through the installed
bridge. Future modules should use `hook_manager.h` and put their verified native
addresses and signatures in `minecraft_build.h`. The manager supports 64 patches
and preserves separate RX relay code and RW data pages. If rollback fails, the
affected hook group stays reserved and its relay stays alive with feature behavior
inactive. This reduces duplicated update-sensitive code; a Minecraft update still
requires deriving and validating the new profile and any changed native ABI.
`AGENTS.md` documents the APIs and the version-update workflow.

`mod_init()` initializes AutoGG, Zoom, and Render after Minecraft has loaded. `auto_gg.cpp` checks
the game's GNU build ID, dispatcher instructions, and native getters through the
shared manager before replacing its TextPacket dispatcher vtable entry;
it preserves the original dispatch. It sends after forwarding a matching incoming
packet, using the live handler's ClientInstance, native TextPacket constructor,
packet sender, and destructors. Game objects are never retained across frames.
Both verified ClientNetworkHandler and LegacyClientNetworkHandler objects are
accepted; their shared TextPacket handler and client layout are checked before
installing the hook. Unverified handlers are rejected with a specific status.
Offsets and libc++ string/optional layouts come from this exact binary's chat
submit path; recheck the sender and packet ABI when updating support.
The build uses `-ffreestanding` to prevent compiler-generated host libc calls;
`runtime.cpp` supplies hidden `memset` and `memcpy` primitives for the Android mod.

This build targets **Android x86_64**, matching your installed Minecraft library.
It needs no Android NDK because this particular example links no standard-library
runtime functions. The X11 and graphics adapters compile against installed host
headers into dependency-free x86_64 objects. Host functions are resolved through the
launcher's host-library API. If you add strings, file access, ImGui, or other
dependencies, use an Android NDK toolchain and libraries for the game's ABI;
ordinary Linux libraries depend on glibc and are not interchangeable.

`api_stubs.cpp` makes three link-time placeholder libraries in `build/` so the
final mod records dependencies on the launcher APIs. **Do not copy those
placeholder libraries into mods.** The launcher supplies the real APIs.

## Check

```sh
g++ -std=c++17 -Wall -Wextra -Werror client.cpp autosprint.cpp test.cpp -o build/test-menu
./build/test-menu
g++ -std=c++17 -Wall -Wextra -Werror test_custom_menu.cpp motion_blur.cpp \
    -ldl -lEGL -lGLESv2 -o build/test-custom-menu
LIBGL_ALWAYS_SOFTWARE=1 MESA_SHADER_CACHE_DIR=/tmp/mcpelauncher-mesa-cache ./build/test-custom-menu
g++ -std=c++17 -Wall -Wextra -Werror test_hook_manager.cpp -ldl -o build/test-hook-manager
./build/test-hook-manager
g++ -std=c++17 -Wall -Wextra -Werror test_auto_gg.cpp hook_manager.cpp -ldl -o build/test-auto-gg
./build/test-auto-gg
g++ -std=c++17 -Wall -Wextra -Werror -DTABLIST_PRESENCE_FIXTURE \
    test_tablist.cpp test_flarial_presence.cpp skin_image.cpp hook_manager.cpp -ldl -pthread -o build/test-tablist
./build/test-tablist
g++ -std=c++17 -Wall -Wextra -Werror test_skin_image.cpp -ldl -pthread -o build/test-skin-image
./build/test-skin-image
g++ -std=c++17 -Wall -Wextra -Werror test_flarial_presence.cpp -ldl -o build/test-flarial-presence
./build/test-flarial-presence
g++ -std=c++17 -Wall -Wextra -Werror test_render.cpp hook_manager.cpp -ldl -o build/test-render
./build/test-render
python3 test_native_build.py /path/to/libminecraftpe.so
g++ -std=c++17 -Wall -Wextra -Werror test_particles.cpp hook_manager.cpp -ldl -o build/test-particles
./build/test-particles
python3 test_analog.py
g++ -std=c++17 -Wall -Wextra -Werror analog_input.cpp test_analog_input.cpp \
    -ldl -o build/test-analog-input
./build/test-analog-input
g++ -std=c++17 -Wall -Wextra -Werror motion_blur.cpp test_motion_blur.cpp \
    -ldl -lEGL -lGLESv2 -o build/test-motion-blur
LIBGL_ALWAYS_SOFTWARE=1 MESA_SHADER_CACHE_DIR=/tmp/mcpelauncher-mesa-cache ./build/test-motion-blur
ln -sfn ../assets build/assets
clang++ -std=c++17 -Wall -Wextra -Werror -I/usr/include/freetype2 test_custom_font.cpp \
    -ldl -lEGL -lGLESv2 -o build/test-custom-font
LIBGL_ALWAYS_SOFTWARE=1 MESA_SHADER_CACHE_DIR=/tmp/mcpelauncher-mesa-cache ./build/test-custom-font
readelf -d build/libblank-client-menu.so
```

The dependency list must contain only `libmcpelauncher_gamewindow.so`,
`libmcpelauncher_menu.so`, and `libmcpelauncher_mod.so`, with no glibc or libstdc++
dependencies. The automated menu check uses mock launcher and X11 callbacks to
check that L routes to the custom menu, Escape closes it, and cursor and sprint
state follow input capture. The custom-menu GLES check covers antialiasing,
opening/closing input timing, scrolling, callback reentry, registration limits,
slider extremes, and control bounds at multiple sizes. The graphics test uses a real headless software-rendered GLES context
to verify pixel blending, alpha, state restoration, history resets, resizing,
and context replacement. The custom font check verifies complete text/icon pixels
under inherited scissor, stencil, rasterizer discard, and color masks, plus
atlas/icon uploads and GL state restoration. These checks require Mesa's software renderer and opens no
desktop window. Checking actual game appearance and performance requires
starting Minecraft and enabling Motion Blur in a world. On load, launcher logs should mention
`Loading mod: libblank-client-menu.so`.

API references:

- https://minecraft-linux.github.io/extra/advanced/modding-api/index.html
- https://minecraft-linux.github.io/extra/advanced/game-window-api/index.html
- https://minecraft-linux.github.io/extra/advanced/menu-api/index.html

Menu theme colors and tint/opacity strengths are defined together in `menu_style.h`.
Button backgrounds use `buttonOpacity`, `enabledButtonOpacity`, and `hoverButtonOpacity` (all default 0.1).
Edit that theme section and rebuild with `bash build.sh` to apply changes.

In `menu_style.h`, `mainButton*` colors control module cards and `settingsButton*`
colors control settings buttons, toggles, and choices. Icons use their original
image colors without a tint. Outline radius values are percentages of panel height;
outline thickness values are pixels, with zero disabling an outline.
`titleMotionDurationNs` sets the title slide duration. `buttonTransitionNs` sets the 180 ms hover/enable transition duration; `mainButtonEnabledHover`, `settingsButtonEnabledHover`, and `enabledHoverButtonOpacity` style hovered enabled buttons. Focused menu theme checks:

```sh
LIBGL_ALWAYS_SOFTWARE=1 MESA_SHADER_CACHE_DIR=/tmp/mcpelauncher-mesa-cache ./build/test-custom-menu --theme
```

### Experimental notification

Enable **Experimental** in the L menu, open chat with the default **T** binding,
type `test`, and send it. Or use **Show test popup** on the Experimental settings
page to preview it directly, including over the menu. A compact popup slides
in from the right using Tablist styling, at 70% of its original dimensions.
The header keeps its size; body text is smaller with a close header gap. Hold **left click for one second** for Yes,
or **right click for one second** for No. Yes and No sit at evenly spaced positions. Holding either button slides its
label horizontally to the center, fades the other label away, and reveals the
loading ring. Releasing early or losing focus cancels the hold and restores
both labels. All inputs keep reaching Minecraft.
The popup stays visible across game screens and the client menu, then hides the title, message, other answer, ring, and timer on completion.
Only Yes or No remains, moving to the dialog center and growing to title size
over 500 ms using the title-size font atlas, then holding for 500 ms before
the popup dismisses. A faint loading bar along the bottom
fills over 10 seconds; if unanswered, the popup closes and reports No.
The timeout continues across focus loss and menus.

Experimental defaults off each launch and controls only the keyboard test
trigger. Pasted text and remapped chat bindings are not supported by that trigger.
The `test` message goes to normal chat.

Other modules can include `popup.h` and call the shared notification API:

```cpp
static void onAnswer(PopupAnswer answer, void* context) {
    // Yes, No, or Cancelled; context belongs to the caller.
}

bool shown = popup_show("My module", "Would you like to continue?", onAnswer, nullptr);
```

The shared popup works independently of Experimental. It copies the title and
message, takes one pending prompt at a time, and returns false if busy or given
empty, multiline, or oversized text (95 bytes for title, 255 for message).
Callers can pass a final `timeoutSeconds` argument (1–3600, default 10).
The timeout starts on the first frame and returns No when it expires.
Yes/No callbacks run on the frame thread outside the popup lock, so callbacks
can open the next prompt. `popup_cancel()` dismisses the current popup and
reports Cancelled on the cancelling thread. `popup_cancel_if(callback, context)`
cancels only a pending prompt owned by that callback and context. Keep callback context alive until
answered or cancelled; keep callbacks short. No native hooks are needed to show
notifications.
