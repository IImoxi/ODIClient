#pragma once

const char* fps_limiter_error();
// Install native hooks only when the low input delay setting is enabled at startup.
void fps_limiter_init(bool lowInputDelayEnabled);
bool fps_limiter_native_supported();
void fps_limiter_native_update(bool enabled, int limit);
void fps_limiter_wait(bool enabled, bool gameplay, int limit);
long long fps_limiter_frame_timestamp_ns();
long long fps_limiter_frame_delta_ns();
