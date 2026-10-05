#pragma once

bool custom_font_draw(const char* text, int centerX, int top, int height,
                      int screenWidth, int screenHeight);
bool custom_font_draw_left(const char* text, int leftX, int top, int height,
                           int screenWidth, int screenHeight);
// Scale glyph geometry from a fixed raster height, retaining fractional display sizes.
bool custom_font_draw_left_scaled(const char* text, float leftX, float top, int height,
                                  float scale, int screenWidth, int screenHeight);
bool custom_font_draw_left_color(const char* text, int leftX, int top, int height,
                                 float red, float green, float blue,
                                 int screenWidth, int screenHeight);
int custom_font_text_width(const char* text, int height);
void custom_font_set_opacity(float opacity);
bool custom_font_draw_icon(const char* assetPath, int centerX, int top, int size,
                           float red, float green, float blue, int screenWidth, int screenHeight);

// Tightly packed 16x16 RGBA skin face, with nearest-neighbor scaling.
bool custom_font_draw_head(const unsigned char* rgba, int centerX, int top, int size,
                          int screenWidth, int screenHeight);

// Honor the caller's scissor while drawing scrollable menu content.
void custom_font_set_clip(bool enabled);

// Reuse settled-size glyphs while the menu zooms; restore to 1 after drawing.
void custom_font_set_raster_scale(float scale);

// Render-thread selection; unavailable Mojangles falls back to Inter.
void custom_font_set_mojangles(bool enabled);

// Standard Minecraft player, fixed 20-degree view; revision identifies copied skin data.
bool custom_font_draw_skin(const unsigned char* rgba, int textureWidth, int textureHeight,
                           unsigned long revision, int centerX, int top, int height,
                           int screenWidth, int screenHeight);
bool custom_font_draw_ring(int centerX, int centerY, float radius, float progress,
                           int screenWidth, int screenHeight);
