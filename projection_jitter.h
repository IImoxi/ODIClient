#pragma once

void projection_jitter_init();
// Called after the finished game frame; advances the offset for the next frame.
void projection_jitter_frame(int width, int height);
const char* projection_jitter_error();
// Live gameplay gate supplied by the client; never retains native camera objects.
bool client_jitter_active();
