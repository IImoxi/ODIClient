# Shared APIs for new modules

These are internal C++ APIs compiled into the mod, not a binary plugin ABI.
Include the specific header you need. Keep feature logic in its module and wire
input/render/lifecycle callbacks in `client.cpp`; declare controls in `menu_pages.cpp`.

| Need | API | Current reuse / constraints |
| --- | --- | --- |
| Observe or hide incoming chat | `chat.h`: `chat_listen` | AutoGG, Chat mods and CC Utils share the existing TextPacket hook. Register before gameplay; callbacks borrow decoded text/live handler only. All observers receive hidden messages too; optional after callbacks run after native forwarding/suppression. |
| Submit a command | `chat.h`: `chat_send_command` | CC Utils and Lobby Scanner share AutoGG’s verified MinecraftCommands execution bridge. Live dispatcher handler only, outside UI locks; accepts optional leading slash and normalizes packet text. False means unavailable/invalid; true means native execution invoked, not server acceptance. |
| Process queued chat/command actions without incoming packets | `chat.h`: `chat_listen_live`, `ChatLiveContext` | Verified ClientInstance LocalPlayer getter supplies a borrowed client during camera/gameplay work; nested callbacks are suppressed. Approved CC Utils and Lobby Scanner actions submit once on the next live callback. |
| Ask for confirmation | `popup.h`: `popup_show`, `popup_cancel_if` | Experimental, Lobby Scanner and CC Utils. Copies title/message; one pending prompt; false means rejected/busy. Timeout answers No. Cancel only your callback/context so you do not dismiss another module's prompt. |
| Build a settings page | `custom_menu.h`: `newPage`, `newTile`, fluent controls | Existing modules share scrolling, layout, input and animations. Declare once in `declare_menu_pages`; 36 pages/tiles, 16 controls per page. Getters must be quick reads. |
| Select and stack corner displays | `custom_menu.h`: `MenuPage::anchor`; `display_layout.h`: frame reset, viewport, placement | FPS Display. Four corner dots in a 16:9 rectangle; each visible display claims its size each frame in stable order. Top stacks grow down; bottom stacks grow up. Frame-thread only; no registry or allocation. |
| Draw a rounded fill, outline, blurred panel or divider | `panel_renderer.h`: `PanelPaint`, `draw_gl_panel`, `draw_gl_divider` | Menu, Tablist and popup. Bottom-left framebuffer coordinates; preserves touched GL state. Use `inheritScissor` for clipped content. |
| Draw text, icons, antialiased progress rings or skins | `custom_font.h`: `custom_font_draw_ring` | Shared renderer for menu and overlays. All positions use top-left pixel coordinates (rings take a center); popup holds and Tablist skin exports share its antialiased ring. Drawing and renderer setters belong on the render thread. Restore opacity, clipping, raster scale and font selection after custom drawing. |
| Animate an overlay/control | `ui_animation.h` | Menu, Tablist and popup use shared quartic/exponential curves. Stateless: caller owns start time, duration, current/from/target values. Exponential uses a host-resolved `exp2f`; no new linked dependency. |
| Persist a module's settings | `client_settings.h` | Typed setting records backed by the existing adapter in `auto_gg.cpp`. Add fields to load/save and tests together. Setters copy state; file saves run outside the UI lock from the frame callback. |
| Validate/install a native hook | `hook_manager.h`: `hooks::*` | AutoGG, Zoom, Render, Tablist and Particles. Exact build/signature gates stay in place. Install serially at `mod_init`; use the existing owner/service for an already-hooked site. |
| Copy/save a skin | `skin_image.h` | Tablist. Allocation is independent of optional export setup; save copies pixels and runs in a worker. One save at a time, bounded skin dimensions. This is a skin export API, not an arbitrary image exporter. |

## Command submission

Call `chat_send_command(message.handler, "/p accept Alex")` from a live native
callback, preferably the after callback when responding to chat. `p accept Alex`
also works: the backend includes the slash exactly once in the native command context. Input
must be nonempty, single line, and at most 255 bytes including the slash. The
backend must be available and gameplay focused. Native string construction/destruction
and command submission remain in the single verified owner in `auto_gg.cpp`.

A popup callback must queue copied text and numeric handler/client identity.
Submit from the next `chat_listen_live` callback only if the client identity still
matches; cancel on disable, world reset, or handler change. MinecraftCommands
receives a copied native command string and a temporary complete
PlayerCommandOrigin (generated UUID, player unique ID and level), and version 52.
It handles remote command packets and local execution/feedback. The boolean
reports invocation, not server acceptance. Neither origin nor client is retained.
The live callback avoids waiting for a subsequent incoming packet; it runs during
native camera/player updates, subject to the normal gameplay/focus gates.

## Incoming chat

```cpp
#include "chat.h"

static bool onMessage(const ChatMessage& message) {
    // Inspect message.text, which has Minecraft formatting codes removed.
    return false; // true hides this packet's native display.
}

void initMyModule() {
    if (!chat_listen(onMessage)) { /* listener registration failed */ }
}
```

Register from `mod_init()` before gameplay. Up to 32 unique listener pairs live
until process exit; registering the same pair again is harmless. Callbacks execute
on the native dispatcher thread, outside the settings/UI locks. Every listener
runs even if a preceding listener hides the message. A second optional callback,
`chat_listen(onMessage, afterMessage)`, runs after native display or suppression;
AutoGG uses an after-only listener to send its response safely after forwarding.
Do not retain `message.text` or `message.handler`; copy needed text, and execute
native actions only while a verified live dispatcher supplies the handler.
Unknown/malformed packet payloads pass through without notifying listeners.
`chat_error()` reports native hook initialization failures; null means available.

Experimental's temporary typed `test` trigger observes keyboard input, rather
than received chat; it stays on its keyboard route to support local sends without
a server echo. New incoming chat features should use this API.

## Boolean settings

Use `MenuPage::toggle(label, setter, getter)` for all On/Off settings. It draws a
left-aligned label and a white translucent switch on the right, with a square
thumb that slides and crossfades its X/check icons over 180 ms in either direction.
The entire row is clickable. Colors and corner radii live in `menu_style.h`.

```cpp
newPage("My module")
    .toggle("Enable", setEnabled, isEnabled)
    .toggle("Extra effect", setEffect, hasEffect).whenEnabled()
    .slider("Strength", 0, 100, setStrength, getStrength).whenEnabled(hasEffect);
```

`whenEnabled()` attaches to the preceding root toggle; dependent toggles share
its group and do not start another group. Use the optional predicate to gate
controls on a dependent toggle as shown above. `choice()` and `dropdown()` are
for named alternatives such as Trail/FPS average and Inter/Mojangles.

`MenuPage::text()` uses smaller description text and compact row spacing,
configured by the `description*Percent` constants in `menu_style.h`.
All settings text uses a settled raster height and fractional glyph scaling
during page transitions. Use `custom_font_draw_scaled()` for centered or left
text at a fixed raster height; `custom_font_draw_left_scaled()` is its left wrapper.

## Anchored displays and stepped sliders

```cpp
newPage("My display")
    .toggle("Enable", setEnabled, isEnabled)
    .slider("Update (ms)", 250, 2000, setInterval, getInterval, nullptr, 250).whenEnabled()
    .anchor("Anchor", setAnchor, getAnchor).whenEnabled();
```

The slider's final optional argument defaults to step 1; the range must span a
whole number of positive steps. Dragging snaps to `minimum + n * step`.
Anchor setters/getters use integers in `DisplayAnchor` order: top-left 0,
top-right 1, bottom-left 2, bottom-right 3. Store the selection in the module's
saved settings. The shared control supports hover, clipping, scrolling,
conditional visibility, and settings zoom; its taller row reserves layout space.

`client.cpp` calls `display_layout_begin_frame()` once before HUD displays.
Every enabled/visible display then claims its actual pixel bounds:

```cpp
#include "display_layout.h"

void drawMyDisplay() {
    DisplayViewport viewport = display_layout_viewport();
    DisplayPosition position;
    int width = custom_font_text_width("My display", 16) + 1;
    if (!display_layout_place(static_cast<DisplayAnchor>(getAnchor()), width, 17, position)) return;
    custom_font_draw_left("My display", position.x, position.top, 16,
                          viewport.width, viewport.height);
}
```

Claim space only when drawing a visible display; include shadows/padding in the
bounds. Call displays in stable order from the frame callback, before the menu.
Each corner has an independent stack with an 8px margin and 4px gap. Top corners
stack downward, bottom corners upward; right corners align the complete display's
right edge. Stacks reset each frame, so disabling/moving a display leaves no gap.
Invalid sizes/anchors, unavailable EGL surfaces, and displays which cannot fit
return false without reserving space. No Minecraft pointers or UI locks are used.

`ui_scale.h` lists the shared 0.5x–6x scale tiers. FPS Display stores the tier index
(default 2 = 1x), scales its framebuffer-dependent base font height, and reuses
the renderer's largest atlas above 60px. Other displays can use the same tiers.

## Settings dividers

Settings rows automatically draw faint white 1-pixel separators and master
toggles have no enclosing bubble. Consecutive descriptions in the same parent
group omit their internal dividers. Consecutive sliders in the same parent group
also omit internal dividers, retaining the boundaries around their group.
Use `.groupWithPrevious()` after another
related control to omit its preceding divider; it leaves visibility unchanged
and only joins the immediately preceding declared item while that item is visible.
Child visibility still uses `whenEnabled()`. Regular controls share compact
`rowHeightPercent`/`rowStridePercent` dimensions. Description rows use the
smaller `descriptionRowHeightPercent`/`descriptionRowStridePercent` dimensions.
Child toggles and sliders share label and right-edge alignment. Render uses
`.groupWithPrevious()` on each slider to join it to its preceding child toggle.
For other UI, reuse `panel_renderer.h`:

```cpp
// Bottom-left framebuffer coordinates; opacity includes your animation fade.
draw_gl_divider(x, y, width, 0.12f * opacity, true);
```

The final argument inherits the caller's scissor when true. The helper preserves
GL state and always draws exactly one framebuffer pixel high. Settings use
`menu_style::settingsDividerOpacity` for the faintness.

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

- Native chat sending and deferred actions currently live in AutoGG.
  Commands already use `chat_send_command` in `chat.h`. Extend this owner for
  additional sender services, preserving live-handler and cancellation rules;
  do not install another TextPacket hook or store native handler pointers.
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
