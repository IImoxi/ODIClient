#pragma once

enum DisplayAnchor { displayTopLeft, displayTopRight, displayBottomLeft, displayBottomRight };
struct DisplayViewport { int width = 0, height = 0; };
struct DisplayPosition { int x = 0, top = 0; };

// Frame-thread only. Call once before drawing HUD displays; resets all four stacks
// and queries EGL framebuffer dimensions. False means there is no usable surface.
bool display_layout_begin_frame();
DisplayViewport display_layout_viewport();
// Claim space only for visible displays, in stable drawing order. Top stacks grow
// down; bottom stacks grow up. Coordinates are top-left framebuffer pixels.
// Returns false for invalid anchors/sizes or when the display cannot fit.
bool display_layout_place(DisplayAnchor anchor, int width, int height, DisplayPosition& position);
