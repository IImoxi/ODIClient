#pragma once

struct MenuColor { float red, green, blue; };
struct PanelPaint {
    MenuColor tint{0.0f, 0.0f, 0.0f};
    float tintAmount = 1.0f;
    float blurScale = 0.0f;
    long long blurTimestampNs = 0; // Positive: reuse quarter-resolution blur for 1/30 second.
    float cornerRadius = 0.0f;
    float opacity = 1.0f;
    bool inheritScissor = false; // Intersect with the caller's active content clip.
    float borderWidth = 0.0f; // Zero fills the shape; positive values draw only its outline.
};

// Bottom-left framebuffer coordinates. Shares the GL adapter in motion_blur.cpp.
// Preserves touched GL state; rounded edges use pixel coverage, without requiring MSAA.
bool draw_gl_panel(int x, int y, int width, int height, const PanelPaint& paint);
