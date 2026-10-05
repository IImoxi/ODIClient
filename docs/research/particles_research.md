# Player-hit particle investigation

Binary: Minecraft 1.26.52.3 Android x86_64, GNU build ID
`3aae9851841480362ffb2b0aabecda2a60b29b27`. Addresses are ELF-relative.
Profile constants and signatures live in `minecraft_build.h`.

## Attack interception

RTTI `8GameMode` and `12SurvivalMode` identifies primary vtables at
`0x172ea3d0` and `0x172ea488`. Their +0x80 slots point to `0x12b99290`
and `0x12b9fb20`. Both wrappers preserve rdi/rsi, move rdx into rcx,
set edx to 1 (attack rather than interaction), and tail-call `0x12b992a0`.
SurvivalMode retains its native +0xc8 gameplay restriction. The other entity
interaction path at `0x12b98770` builds the same type-3 use-item-on-entity
transaction but receives an explicit action. We intercept only the attack slots.

The common attack implementation reads the initiating Player at GameMode +8
(`0x12b992cd`) and preserves the entity argument in r12 and the item argument in
r13. Its transaction closure captures them at `0x12b994d6` and `0x12b994f2`.
The wrapper ABI is bool(GameMode*, Actor*, const ItemStack*). The replacement
forwards every argument, preserving native restrictions and return values.

## Native critical emitter

RTTI identifies LocalPlayer and RemotePlayer primary vtables at `0x16fdd4c8`
and `0x16fddcd0`. LocalPlayer +0x6a8 points to `0xc215c20`. That wrapper accepts
(LocalPlayer*, Actor*, int), loads the local particle context from +0xc88,
passes `minecraft:critical_hit_emitter` (30 bytes), and tail-calls `0xc410cb0`.
The same body is called by the native entity-event dispatcher at `0xc5b9cf5`
for event 4; event 5 supplies the magic-critical emitter instead. This confirms
that the wrapper creates the cosmetic effect rather than modifying attack damage.

The emitter body obtains the particle renderer from its context (+0x5e0 virtual
getter), sets the effect's count from the integer argument, reads the target's
native shape/position/dimension, and creates the effect through the renderer.
The mod requests count 16, using native textures, movement, lifetime, and world
projection. No separate position cache or overlay renderer is required.

Only attacks initiated by the exact LocalPlayer class emit effects, excluding
server-side Player GameModes in integrated worlds. Targets must be exact
RemotePlayer or LocalPlayer classes. Native objects are used synchronously before
forwarding the original attack, so target removal cannot leave a cached pointer.
The module is inactive unless its hook install succeeded and focused gameplay
was observed by the frame callback. Both original wrappers remain callable if
installation is Retained. Installation uses the shared hook manager exclusively.

## Validation and limits

`python3 tests/test_native_build.py libminecraftpe.so` checks the build ID, both attack
slots and complete wrapper signatures, initiating-player field read, player RTTI identities, critical-emitter
slot/signature/string/context displacement/tail-call, and emitter entry bytes.
`tests/test_particles.cpp` checks target filtering, enable/gameplay/context guards,
original arguments/returns, both executable wrapper paths, and corruption gates.
The persistence test covers version-12 migration and the saved toggle.

These static and mock checks cannot verify visible particles in a running world.
The effect follows a native attack attempt, even if the server denies damage;
empty-air swings and players beyond native reach do not trigger it. Custom
Actor subclasses are excluded until their identities are verified. Enable the
module and test repeated clicks on a player in survival and creative, movement,
world changes, and menus before treating the appearance as verified.
