# Deferred Lobby Scanner confirmations

Verified against Minecraft 1.26.52.3 Android x86_64,
GNU build ID `3aae9851841480362ffb2b0aabecda2a60b29b27`.

Popup callbacks execute later on the frame thread. A network handler or client
pointer cannot be retained until then. The callback instead approves copied
text; a subsequent native dispatch borrows its live handler to use the existing
verified chat/CommandRequestPacket bridge. The text dispatcher and Tablist's
existing roster/skin observers provide opportunities, with these additional
incoming dispatchers covering regular server activity:

| Packet | RTTI name address | Dispatcher slot | Function | Handler slot | Base / legacy handler |
| --- | --- | --- | --- | --- | --- |
| SetTime | `0x2fd3253` | `0x170fb110` | `0xe78c1c0` | `0x118` | `0xb129b20` / `0xc5b0ee0` |
| MovePlayer | `0x2fd36f4` | `0x170fb620` | `0xe78c9a0` | `0x178` | `0xb129bc0` / `0xc5b6660` |
| NetworkStackLatency | `0x2fd7288` | `0x170ff2c0` | `0xe792ce0` | `0x5d0` | `0xb12a010` / `0xc5c2630` |

Derivation: find the ELF string
`31PacketHandlerDispatcherInstanceI<packet-type>Lb0EE`, follow its RELATIVE
relocation to type_info, then the type_info relocation to its vtable. Dispatch
is the vtable's third callable entry, matching the independently verified Text
packet dispatcher ABI. Each thunk has these exact 18 bytes:

`48 89 d7 48 8b 11 48 8b 07 48 8b 80 <handler-slot: uint32 LE> ff e0`

It moves the borrowed handler from RDX to RDI, reads the packet from RCX's
shared_ptr, keeps the network identifier in RSI, and tail-calls the handler
virtual method. Both exact client-handler vtables were checked through ELF
relocations. No packet payload layout is assumed or read by these hooks.

All offsets live in `minecraft_build.h`. AutoGG gates all four dispatcher
patches before installing one shared-manager batch. Every wrapper forwards the
original, including when inactive or installation is retained. The native
checker validates the thunk bytes, dispatcher relocations, and both handler
vtable targets. Fixture checks cover Yes/No, command/public-chat mode, one send,
focus gating, busy prompts, settings cancellation, and session changes.

Numeric handler identity is only compared, never dereferenced later. StartGame,
Disconnect, and Tablist handler changes invalidate confirmations; settings
changes do likewise. A different valid handler on subsequent traffic cancels
the old confirmation. Unknown/server-side handler types are ignored. A quiet
connection can delay a confirmed send until the next observed packet; native
objects are always borrowed from that dispatch, never reacquired from stale
addresses. Re-derive these dispatchers and handler slots for any new game build.

## Shared command text repair (2026-10-06)

Target: Minecraft 1.26.52.3 Android x86_64 `libminecraftpe.so`, SHA-256
`2540a1b65de5ed796215c33efc00e2f1dc7fdb8e806cb9783797557dd21fb002`.
Focused ELF disassembly shows:

- `0xe268c10` initializes the default CommandRequestPacket, including empty
  command string at `+0x30`, player origin type zero at `+0x48`, version 52 at
  `+0x80`, and internal flag false at `+0x84`.
- The populated constructor `0xe268dd0` moves the supplied string directly into
  `+0x30`; it does not prepend a slash. Its native caller at `0x119312e7`
  supplies the original input string (short string copy at `0x11930ddc` or long
  string construction at `0x119312aa`).
- Our caller stripped the slash in Lobby Scanner and never added it for party
  accepts. The shared `chat_send_command` service now normalizes the packet text
  to `/hub` or `/p accept NAME`, preserving the gated constructor/sender.

The missing slash is the suspected cause of the server-side failure. Static
inspection establishes packet layout and unchanged input copying; actual server
acceptance still requires in-game verification. No profile gates were relaxed.

## Native command service repair

The user reports no execution or unknown-command response after slash repair;
that hypothesis did not resolve the failure. Further focused disassembly of the
same ELF establishes the path used by the chat screen:

- `0xb56b7c2` calls ClientInstance slot `+0x6d0` with sret. Its vtable target
  `0xa8030d0` obtains the service at ClientInstance `+0x638`, retaining its
  lifetime token and returning a 24-byte reference whose service is at `+16`.
- `0xb56b852..0xb56b85f` passes the service, command string, optional string
  output, empty request string, and source value 4 to `0xa546ba0`.
- `0xa546d70` delegates to the service’s native execution/request path
  `0xa541310`, which obtains live context through the service client at `+0xa0`.
- The native reference destructor is `0xa59b270`; optional output engagement
  is at byte 24. Strings and reference are destroyed within the same dispatch.

`chat_send_command` now follows this service path instead of constructing a
CommandRequestPacket with a default empty origin. New entry signatures and the
ClientInstance getter slot are checked before installing the existing hook.
No additional hook is installed. Guards reject absent/expired service lifetime
before invoking the getter, which otherwise assumes a live service. Static
inspection establishes ABI parity; execution on a real server remains unverified.

## Correct execution path and prompt timing repair

Further user testing established that `0xa546ba0` sent literal public chat text.
The prior service identification above was wrong; the executable path now uses
MinecraftCommands and does not invoke that chat submission function.

Focused disassembly/ELF relocations of the same exact binary establish:

- ClientInstance slot `+0x538` -> `0xa8001b0` returns the Minecraft object at
  client `+0x190`. Native code at `0xb487600` reads Minecraft `+0xb8` for the
  MinecraftCommands object.
- `0x1193dd80` builds a 40-byte CommandContext (native string `+0`, origin
  pointer `+0x18`, version `+0x20`) and calls `0x11930b50` synchronously before
  destroying its owned origin. Our call borrows a stack context/origin instead.
- Chat-screen origin construction `0xb56a662..0xb56a69b` initializes the 40-byte
  PlayerCommandOrigin: vtable `0x17235018`, generated UUID at `+8`, player unique
  ID at `+0x18`, and Level at `+0x20`. UUID generator `0x16bcfb20(0)` returns
  its two words in RAX/RDX. `0x126dc990(player)` returns the unique ID pointer;
  the native Level field is player `+0x1d0`.
- `0x11930b50` handles remote requests and local execution/feedback. Its remote
  branch invokes origin data slot `+0xd8` (`0x118c4290`) and populated packet
  constructor `0xe268dd0`, preserving the actual name within the command string.
- ClientInstance LocalPlayer getter slot `+0x100` -> `0xa7efaa0` is called by
  native camera function `0xaba3af0` at `0xaba3c75`; this is the same function
  containing the verified Zoom FOV site `0xaba3eff`. Another camera caller is
  `0xaba3aaf`. Thus queued confirmations can run during gameplay updates without
  waiting for another incoming chat/time/latency packet.

The existing AutoGG backend owns an additional getter-vtable patch and exposes
`chat_listen_live`/`ChatLiveContext`. It always forwards the getter, preserves its
result, suppresses observer reentry, and borrows the client only within that call.
Approved actions carry copied names/text and numeric client/handler identity and
are consumed before invoking native execution. Existing disable/world/handler
cancellation remains. Signatures, getters, origin type/data slots and build ID
are checked before installing the five-site shared batch. Static evidence shows
ABI/path parity; real-server execution and update latency need in-game checking.
