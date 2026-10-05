#pragma once

// Stateless curves shared by overlays and menu controls. Pass normalized time
// (elapsed / duration); endpoints clamp to [0, 1]. Callers own timing/state.
namespace ui_animation {
inline float ease_out_quart(float t) {
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    float remaining = 1.0f - t;
    return 1.0f - remaining * remaining * remaining * remaining;
}

// Supply the host-resolved exp2f: the Android mod cannot link host libm.
// The function must be available before calling with time inside (0, 1).
inline float ease_out_exponential(float t, float (*exp2f)(float)) {
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    return 1.0f - exp2f(-10.0f * t);
}
}
