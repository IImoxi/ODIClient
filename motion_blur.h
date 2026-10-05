#pragma once
void motion_blur_render(bool enabled, float strength, float averageFrames, bool screenBlur = false,
                        int averageHz = 0, long long frameTimestampNs = 0);
const char* motion_blur_error();
