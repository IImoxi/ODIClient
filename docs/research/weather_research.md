# Environment local weather (Minecraft 1.26.52.3 x86_64)

Static evidence from the supported `libminecraftpe.so` (build ID
`3aae9851841480362ffb2b0aabecda2a60b29b27`):

- RTTI `7Weather` at `0x34155fd`, typeinfo `0x173c9fe0`, primary
  vtable `0x173c9dd8`. Dimension owns Weather at `+0x1b8`.
- Weather tick `0x13a3b800` borrows its Dimension at `+0x58`; Dimension
  borrows Level at `+0xa0`. Dimension flag `+0xe0` gates weather processing.
- Dimension tick directly calls Weather tick at `0x13aba195`.
- `0x13a3b81d` copies current rain `+0x38` to previous `+0x34`, and current
  thunder `+0x44` to previous `+0x40`. Tick interpolates toward untouched
  rain/thunder targets `+0x3c`/`+0x48`; counter `+0x30` advances.
- `0x13a3b8c3` calls Level slot `+0x9f0` (client check); client branch skips
  native LevelData writes at `0x13a3b8f8` and `0x13a3b905`. The override
  additionally requires exact ClientLevel RTTI/vtable `0x16f40a38` and
  Overworld Dimension vtable `0x16deee38`, excluding server dimensions.
- Native precipitation interpolation is inlined, for example
  `0xccc958b` reads Weather `+0x34/+0x38`. Overriding their copied
  values after the native client tick leaves biome precipitation selection native.

The trampoline replays ten complete prologue bytes, including the Dimension
load, before returning to `0x13a3b80a`; the owned patch changes eight bytes.
No target intensity, LevelData, weather timers, packets or commands are changed.
Before forwarding the next tick, restore only scalar values that still match
the prior override and whose weather/dimension identity and tick match; native
changes take precedence. New worlds replace copied history without dereferencing
the old object. One live client Overworld is supported.

Static evidence establishes the ABI and client/server branch, not execution.
Behavior tests check clear/rain/thunder interpolation, native forwarding, packet
changes, disable restoration, server exclusion and exact-build gates. Real-world
visual routing, audio, snow biomes and native/custom sky appearance still require
in-game verification. Thunder intensity does not generate server lightning.
