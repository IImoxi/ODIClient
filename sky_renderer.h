#pragma once

// Replaces a verified native Sky draw with a camera-directed full-screen sky.
// Native celestial draws are suppressed only after replacement in the same frame.
void sky_renderer_init();
const char* sky_renderer_error();
const char* sky_renderer_status();
