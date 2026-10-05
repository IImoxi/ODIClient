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
