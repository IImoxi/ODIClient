#pragma once

// Shared fitting/font scale tiers. Font sizes above 60px reuse the largest atlas.
namespace ui_scale {
inline constexpr int percentages[] = {50, 75, 100, 125, 150, 200, 250, 300, 400, 500, 600};
inline constexpr const char* labels[] = {"0.5x", "0.75x", "1x", "1.25x", "1.5x", "2x", "2.5x", "3x", "4x", "5x", "6x"};
inline constexpr int count = sizeof(percentages) / sizeof(percentages[0]);
}
