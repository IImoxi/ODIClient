# REA trial and native mod candidates

Date: 2026-10-05. This is a bounded static investigation, not an implemented
module or a verified hook profile.

## Target and tool result

- Artifact: `/home/iimoxi/.local/share/mcpelauncher/versions/1.26.52.3/lib/x86_64/libminecraftpe.so`
- ELF: Android x86_64, stripped, approximately 381 MiB.
- GNU build ID: `3aae9851841480362ffb2b0aabecda2a60b29b27`.
- SHA-256 returned by REA: `2540a1b65de5ed796215c33efc00e2f1dc7fdb8e806cb9783797557dd21fb002`.
- REA opened the artifact with Ghidra 12.1.4. Its default analysis profile
  digest was `5e1923a0faf0070b1b2698a03a69498a7d04f7ad0566c7b6cd4bb20e944f0454`.
- A case-insensitive regex procedure search for
  `hurt|bob|gamma|latency|cooldown` timed out after 300 seconds. Ghidra logs
  showed repeated `LSDACallSiteTable` errors about function bodies not containing
  landing pads/call-site areas. This does not establish why the timeout happened.
- No REA procedure, xref, or decompilation evidence was returned. No REA Evidence
  IDs are available for the feature findings below. The session was closed.

The fallback used `file`, `readelf -n`, direct mapped-byte string searches,
candidate RIP-relative LEA scans of executable ELF segments, and targeted
`objdump -d`. LEA candidates were corroborated in the disassembly. Addresses
below are ELF virtual addresses, not runtime pointers; stripped objdump's
nearest exported-symbol labels do not identify these functions.

## Finding ledger

| Candidate / player benefit | Observed static evidence | Inference and unresolved work |
| --- | --- | --- |
| Camera comfort preset: quick access to bobbing and shake controls | At `0xc3dde3c`/`0xc3dde43`, instructions load `options.viewBobbing`/`gfx_viewbobbing`, then call `0xc473420` at `0xc3dde65`; nearby immediate is `0x26`. At `0xc3de06a`/`0xc3de07f`, code builds strings `options.screenShake`/`camera_shake`, supplies immediate `0x23a`, and calls `0xec1c470` at `0xc3de0b7`. | These look like native option registration paths. An ODIClient preset could expose them conveniently. Consumers, getter/setter ABI, and actual effects remain unverified. Camera shake is not proven to include damage tilt; do not claim a no-hurt-camera hook from this evidence. |
| FOV effects toggle alongside Zoom | `0xc3dffca` references `options.fov.toggle`; the following string at `0x274fcd2` is `gfx_field_of_view_toggle`. Code supplies `0x1b2` at `0xc3e0009`. Separate camera component strings include `MinecraftCamera::GameplayAffectsFovComponent` and `minecraft:gameplay_affects_fov`. | This is a lead for controlling gameplay-driven FOV changes. Its relation to sprinting, bow use, and the existing Zoom sites still needs consumer tracing. |
| Brightness hotkey / preset | `0xc3e0430` loads `options.gamma`; `0xc3e0445` loads `gfx_gamma`. The registration-like call at `0xc3e0495` targets `0xec1b130`, with immediate `0x32`. Float inputs loaded into xmm0/xmm1/xmm2/xmm3 are `0.5`, `0`, `1`, and approximately `0.001`. | Likely a native brightness option with default/range/step inputs; parameter meanings are not fully derived. A preset inside the native range is a plausible small module. Fullbright beyond that range is not established. |
| Latency diagnostics | `0xc3e5995`/`0xc3e59aa` build `options.dev_showLatencyGraph`/`dev_show_latency_graph`; code supplies immediate `0x124` and calls `0xec1c470` at `0xc3e59e5`. | A developer-labelled option exists in registration code. The graph may be gated or unavailable in this retail build. It is not proof of a usable ping HUD or per-player latency. |
| Coordinate-copy convenience | Direct references to `key.copyCoordinates` and `key.copyFacingCoordinates` occur at `0xc7e1b00` and `0xc7e1b56`; this procedure compares input strings. Additional references occur at `0xaacb5b8`, `0xaacb65d`, `0xb29effb`, and `0xb29f086`. | Native action names offer a lead for copying current/target coordinates. Actual action dispatch, permissions, clipboard bridge, and Wayland behavior need investigation. These references alone do not prove clipboard execution. |
| Durability warning / HUD | `query.remaining_durability` is referenced by a LEA at `0x12a3d764`; strings describe remaining/max durability and RTTI names include ScriptItemDurabilityComponent getters. | Lower-confidence lead. No client inventory getter ABI or HUD call path was derived; script reflection types are not a safe native mod API. |

## Recommended next investigation

Start with the camera comfort preset: follow the option registration and its
readers, then reuse the existing version-gated option access approach if the
ABI matches. First check whether Minecraft's own settings already satisfy the
desired behavior; the mod's added value would be a convenient preset/hotkey.

Before implementation, derive getter/setter semantics, native consumers, exact
signatures and lifetime/threading rules. Keep values in `minecraft_build.h`,
reuse the shared hook manager where hooks are needed, and verify in game. None
of the candidate addresses here is authorized as a safe patch site merely by
being listed.

Coverage: target identity and the REA smoke test are answered; camera/options
registration is partially answered through fallback disassembly; FOV consumers,
latency rendering, clipboard behavior, durability access, and runtime usefulness
remain unresolved. No game binary, module source, or installed mod was changed.
