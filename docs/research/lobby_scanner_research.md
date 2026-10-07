# Lobby Scanner confirmation and command path

Target: Minecraft 1.26.52.3 Android x86_64, build ID
3aae9851841480362ffb2b0aabecda2a60b29b27. Dispatcher, handler, getter, and
command profile data are centralized in minecraft_build.h.

Popup callbacks run later, so they retain copied action text and numeric
handler/client identity only. Yes approves the action; it is submitted during a
live gameplay callback using objects borrowed for that callback. StartGame,
Disconnect, handler changes, settings edits, and disabling cancel pending
actions. The TextPacket, SetTime, MovePlayer, and NetworkStackLatency
dispatchers are signature/relocation gated and always forward native calls.
The player-getter callback lets approved actions run during gameplay updates
without waiting for another incoming packet.

## Correct command execution

Two earlier fixes were insufficient: adding the slash did not resolve server
execution, and the service first mistaken for command execution sent literal
public chat. The working path follows Minecraft's chat screen into
MinecraftCommands. It builds a stack CommandContext and PlayerCommandOrigin
from the live player identity and level, then invokes the native command
executor, which handles local feedback and remote command requests. No native
object is saved beyond the callback.

The command constructor copies its input unchanged, so slash normalization
still belongs at the caller. Exact-build gates cover the native entry points
and origin ABI; test fixtures cover approval, cancellation, send-once behavior,
mode, and focus gating. Real-server acceptance and update latency still need
in-game verification. Re-derive the profile for every game build.
