# Native mod research leads

Date: 2026-10-05. Target: Minecraft 1.26.52.3 Android x86_64,
build ID 3aae9851841480362ffb2b0aabecda2a60b29b27. This is static
investigation, not a verified hook profile. REA returned no procedure evidence;
the leads below came from string/relocation searches and focused objdump.

| Lead | Finding | Still unknown |
| --- | --- | --- |
| Camera comfort | Native option registrations reference view bobbing and screen shake. | Consumers and getter/setter ABI; shake is not proven to include damage tilt. |
| Gameplay FOV | A native FOV-effects toggle and GameplayAffectsFov component exist. | Effects on sprint/bow and relation to Zoom. |
| Brightness | Native gamma option appears to accept a bounded value. | Parameter semantics; fullbright beyond the native range is not established. |
| Latency | A developer latency-graph option is registered. | Whether it is available in retail or provides usable ping data. |
| Coordinate copy | Native current/target coordinate key names are present. | Dispatch, permissions, clipboard path, and Wayland behavior. |
| Durability | Script durability queries and component getters are present. | Safe client inventory access or HUD path. |

Camera comfort is the best next lead: first check whether Minecraft settings
already cover the need, then trace consumers and derive the ABI before
implementation. None of these strings or addresses alone establishes a safe
patch site. Re-verify signatures and runtime behavior before using any lead.
