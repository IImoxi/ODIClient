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

Open the menu with **L**. On a module tile, left-click toggles it; right-click opens its settings. Press **Escape** to return to the tiles, then press it again to close the menu. Settings are saved beside the installed mod, except Experimental, which resets off each launch.

| Feature | What it does |
| --- | --- |
| **Zoom** | Hold **C** and scroll to zoom from 1.5× to 30×. Change the key and scroll step in settings. Defaults off. |
| **Tablist** | Hold **Tab** to view the roster. Scroll to select a player and preview their skin; hold right-click for one second to save the skin as a PNG. Defaults on. |
| **Auto Sprint** | Simulates Left Ctrl while moving forward, using Minecraft’s default bindings. |
| **Particles** | Shows local critical-hit particles on player attack attempts. Cosmetic only; does not change damage. Defaults off. |
| **Motion Blur** | Blends recent frames or averages over a selected frame-time window. Requires EGL with OpenGL ES 3 or desktop OpenGL 3.3. |
| **Screen Blur** | Experimental full-screen spatial blur, including HUD and menus. The ODIClient menu remains sharp. Defaults off. |
| **FPS Limit** | Caps rendering from 30 to 480 FPS. Minecraft’s FPS setting or VSync may impose a lower cap. |
| **Render limits** | Experimental vertical terrain-section limits. Can hide visible terrain; defaults off. |
| **AutoGG** | Sends a configurable response to matching incoming chat. Defaults to matching “You won the game” and sending “gg”. |
| **Chat mods** | Optionally hides incoming messages that match a comma-separated keyword list. |
| **Lobby Scanner** | Runs configured commands or sends messages when matching players join. Rules can require confirmation before sending. |
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

## Troubleshooting

- **ODIClient is missing from Installed Mods:** restart MCPELauncher and confirm the build installed under the launcher’s mods directory. Pass the correct directory to `build.sh` if needed.
- **A feature reports an unsupported Minecraft build:** native hooks are restricted to the verified game build listed above.
- **Motion Blur is unavailable:** check that the active graphics context supports OpenGL ES 3 or OpenGL 3.3.
- **A module does not respond:** check that it is enabled in the L menu. Zoom also requires holding its configured key during gameplay.

## Credits and licenses

The bundled Inter font is licensed under the SIL Open Font License 1.1; see [`assets/OFL.txt`](assets/OFL.txt).
