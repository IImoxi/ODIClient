#pragma once

constexpr unsigned long skin_image_max_bytes = 256 * 256 * 4;
// Allocation remains usable if optional PNG export setup fails.
bool skin_image_init();
unsigned char* skin_image_allocate(unsigned long bytes);
void skin_image_release(unsigned char* pixels);
// Copies the image before returning. One background save at a time.
bool skin_image_save(const unsigned char* pixels, unsigned int width, unsigned int height, const char* name);
// 0 idle, 1 saving, 2 saved, -1 failed.
int skin_image_save_status();
