#pragma once

const char* fps_limiter_error();
void fps_limiter_wait(bool enabled, bool gameplay, int limit);
long long fps_limiter_frame_timestamp_ns();
long long fps_limiter_frame_delta_ns();
