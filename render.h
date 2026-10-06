#pragma once

void render_init();
const char* render_error();
// Opt-in diagnostic output, drained once per second from the frame thread.
void render_trace_frame(long long frameDeltaNs);
