# Shared APIs for new modules

These are internal C++ APIs compiled into the mod, not a binary plugin ABI.
Include the specific header you need. Keep feature logic in its module and wire
input/render/lifecycle callbacks in `client.cpp`; declare controls in `menu_pages.cpp`.

| Need | API | Current reuse / constraints |
| --- | --- | --- |
| Ask for confirmation | `popup.h`: `popup_show`, `popup_cancel_if` | Experimental and Lobby Scanner. Copies title/message; one pending prompt; false means rejected/busy. Timeout answers No. Cancel only your callback/context so you do not dismiss another module's prompt. |
| Build a settings page | `custom_menu.h`: `newPage`, `newTile`, fluent controls | Existing modules share scrolling, layout, input and animations. Declare once in `declare_menu_pages`; 36 pages/tiles, 16 controls per page. Getters must be quick reads. |
| Draw a rounded fill, outline or blurred panel | `panel_renderer.h`: `PanelPaint`, `draw_gl_panel` | Menu, Tablist and popup. Bottom-left framebuffer coordinates; preserves touched GL state. Use `inheritScissor` for clipped content. |
| Draw text, icons, progress rings or skins | `custom_font.h` | Shared renderer for menu and overlays. All positions use top-left pixel coordinates (rings take a center). Drawing and renderer setters belong on the render thread. Restore opacity, clipping, raster scale and font selection after custom drawing. |
| Animate an overlay/control | `ui_animation.h` | Menu, Tablist and popup use shared quartic/exponential curves. Stateless: caller owns start time, duration, current/from/target values. Exponential uses a host-resolved `exp2f`; no new linked dependency. |
| Persist a module's settings | `client_settings.h` | Typed setting records backed by the existing adapter in `auto_gg.cpp`. Add fields to load/save and tests together. Setters copy state; file saves run outside the UI lock from the frame callback. |
| Validate/install a native hook | `hook_manager.h`: `hooks::*` | AutoGG, Zoom, Render, Tablist and Particles. Exact build/signature gates stay in place. Install serially at `mod_init`; use the existing owner/service for an already-hooked site. |
| Copy/save a skin | `skin_image.h` | Tablist. Allocation is independent of optional export setup; save copies pixels and runs in a worker. One save at a time, bounded skin dimensions. This is a skin export API, not an arbitrary image exporter. |

## Confirmation example

```cpp
#include "popup.h"

static void onAnswer(PopupAnswer answer, void*) {
    if (answer == PopupAnswer::Yes) {
        // Queue the module's action; execute it where its native context is valid.
    }
}

bool askToContinue() {
    return popup_show("My module", "Continue?", onAnswer);
}

void cancelMyPrompt() {
    popup_cancel_if(onAnswer, nullptr);
}
```

Callbacks run outside the popup lock: answers on the frame thread, cancellation
on the cancelling thread. A non-null context must remain alive until the callback
finishes. Never retain borrowed Minecraft objects in a callback context. Show
failure does not call the callback; handle it in the requesting module.

## Animation example

```cpp
#include "ui_animation.h"

float t = static_cast<float>(frameNs - startNs) / durationNs;
float opacity = from + (target - from) * ui_animation::ease_out_quart(t);
```

Use a positive duration and the same monotonic timestamp source for start and
frame times. To reverse smoothly, evaluate the current value first, then use it
as the new `from` and restart the timestamp.

## Candidates to extract when another feature needs them

- Native chat/command sending and deferred actions currently live in AutoGG.
  Lobby Scanner already shares that sender. A new sender client should get a
  typed service from this owner, preserving live-handler and cancellation rules;
  it should not install another TextPacket hook or store native handler pointers.
- Roster events are owned by Tablist and currently notify Lobby Scanner directly.
  If another module needs joins/leaves, add a copied-data observer at that point
  rather than another native hook.
- Popup and skin export both use timed mouse holds, but differ in passive versus
  consumed input, selection invalidation and completion state. Share their hold
  state machine only when another interaction establishes a common contract.
- Host symbol loading appears in several adapters. Their required libraries,
  retry behavior and thread ownership differ; a generic loader would need to
  preserve these before replacing them.

No module registry or general event bus is needed for the current feature set.
