# Loaded-player camera targeting

Target: Minecraft 1.26.52.3 Android x86_64, build ID
3aae9851841480362ffb2b0aabecda2a60b29b27. Evidence is static ELF/RTTI
analysis; exact-build gates are in minecraft_build.h.

The shared player_target service reconstructs the center camera ray from the
native view/projection matrices, then synchronously iterates loaded players and
selects the nearest ray/AABB hit. Only verified LocalPlayer/RemotePlayer
classes in the same level and dimension qualify. It copies the name for the
caller; camera, actor, and callback pointers do not outlive the live-client
callback. No new native hook or interaction behavior is added.

“Unlimited distance” means no additional reach limit among players currently
loaded by Minecraft. Block occlusion is ignored, so a player behind a block can
win. Third-person/custom camera alignment and live targeting still need in-game
checks. Invalid names on the nearest hit suppress farther matches.

Native-build checks cover addresses/signatures and the player iterator;
fixtures cover ray unprojection, nearest AABB, exclusions, malformed names, and
unsupported data. These checks do not establish runtime camera alignment.
