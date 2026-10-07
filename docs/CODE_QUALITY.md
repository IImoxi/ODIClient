# Code quality and growth review

Reviewed 2026-10-07. The code is acceptable for gradually adding small modules.
Keep the existing typed APIs and shared hook ownership; a general module
framework would add complexity without resolving the main maintenance concerns.
This is an architecture and cleanup review, supported by regression checks,
not a proof of every native lifetime, thread interaction or in-game behavior.

## What supports growth

- `hook_manager` centralizes exact-build validation, patch ownership, permission
  changes and rollback. Features fail closed on unsupported builds.
- Chat/live-client observers, popup, panel/font drawing and MenuPage builders
  already provide useful reuse. Native borrowed objects are scoped to callbacks;
  deferred work uses copied values and identity checks.
- Roster, texture, UI registration and listener storage have explicit bounds.
  Tests cover native forwarding, signature gates, input cancellation, settings
  migration and real GLES pixels/state restoration.
- Build/install now fingerprints actual source inputs and compiler identity,
  records the build ID in `build-info.txt`, and verifies installed binary bytes and SHA-256.

## Maintenance concerns

| Location | Assessment and next step |
| --- | --- |
| `auto_gg.cpp` | About 1,500 lines combine the native chat/command bridge, several feature behaviors and all setting persistence. Extract persistence behind the existing `client_settings.h` API when adding another substantial settings family; preserve migration tests and the native bridge. |
| `custom_menu.cpp` | About 1,900 lines combine layout, input and rendering. Shared controls are preferable to feature-specific drawing. Extract a cohesive layout/input component when changing that area substantially; avoid a general UI framework. |
| `custom_menu.cpp`, `chat.cpp`, `hook_manager.h` | Fixed limits (36 pages/tiles, 16 controls per page, 32 listeners per list, 64 patches) are appropriate today. Check registration/install errors when adding features; raise limits only when actual additions need them. |
| `auto_gg.cpp::saveConfig()` | Saves use a temporary file and rename outside the settings lock, but clear the dirty flag before I/O. A failed write reports an error and does not retry until another change. A future persistence extraction should add bounded retry coverage. |
| Module spin locks and GL context caches | Keep critical sections short and callbacks/I/O outside locks. GL resources must remain tied to their owning context. Headless tests do not establish hardware performance or full runtime thread safety. |

## Cleanup performed

Removed the failed buffer-reuse, occlusion, weather/particle suppression,
multi-draw and uniform-reuse implementations, their obsolete tests and native
profiles. Preserved research records and zero-filled historical CSV columns.
The former uniform cache is now `gpu_shader_services`: only shader source,
compile, link, delete and bind interception remains, with a focused forwarding
and gate test. No retired experiment is linked into the normal mod.

Sky GPU caching, half-resolution atmosphere and reduced samples are always
active. Old config slots remain for compatibility but cannot disable them.
Alternative quality paths exist only as test options for pixel comparisons;
unsupported GPU texture paths fall back to direct reduced-sample atmosphere.
These optimizations trade some gradient accuracy for less work; hardware FPS
benefit and the final appearance still require an in-game comparison.

## Validation

Passed: actual Minecraft ELF profile checks; shader-service gates/forwarding;
GLES sky and menu pixels/state restoration; Environment; Render and GL/frame
tracing; AutoGG native fixture and settings migration; hook manager; Zoom;
Particles; Tablist; SDL and client input. The stale AutoGG fixture was updated
to include the existing native chat-presentation gates, without weakening them.
Two unchanged builds produced byte-identical binaries. Installer checks cover
staged and same-folder installation, absence of a build ID in the menu footer, removed sky control
labels, and rejection of a mismatched package. Final Android dependencies are
only the three launcher ABI libraries.
