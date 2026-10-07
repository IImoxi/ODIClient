# Player-hit particle investigation

Target: Minecraft 1.26.52.3 Android x86_64, build ID
3aae9851841480362ffb2b0aabecda2a60b29b27. Profile addresses and signatures
are in minecraft_build.h.

The GameMode and SurvivalMode attack slots reach the common native attack path.
Its LocalPlayer critical-emitter wrapper creates the cosmetic
minecraft:critical_hit_emitter effect; it does not change damage. Particles
reuses that emitter with count 16, native textures, movement, and lifetime.
The shared hook manager preserves native arguments, return values, and
SurvivalMode restrictions.

Effects run only for attacks initiated by the verified LocalPlayer class and
verified LocalPlayer/RemotePlayer targets. Native objects are used
synchronously; no actor pointer is retained. Normal reach and attack selection
remain native, so server-rejected attack attempts may still show the effect;
air swings, mobs, blocks, and out-of-reach players do not.

Build-ID/signature and executable-fixture checks cover the wrappers, emitter,
filters, and forwarding. Appearance still needs in-game verification in
survival and creative, including movement and world/menu changes. Unverified
Actor subclasses are excluded.
