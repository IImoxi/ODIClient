#pragma once

// Frame-thread only; uses the existing monotonic frame timestamp.
void fps_display_render(bool focused, long long frameNs);
