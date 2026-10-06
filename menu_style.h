#pragma once
#include "panel_renderer.h"

namespace menu_style {
constexpr MenuColor rgb(unsigned int hex) {
    return {((hex >> 16) & 255) / 255.0f, ((hex >> 8) & 255) / 255.0f, (hex & 255) / 255.0f};
}
// Menu theme: edit colors and their opacity/tint strengths here.
constexpr MenuColor background = rgb(0x1c1920); // Title bar.
constexpr MenuColor contentBackground = rgb(0x101010);
constexpr MenuColor text{0.94f, 0.93f, 0.95f};
constexpr MenuColor border = rgb(0xffffff);
constexpr MenuColor action = rgb(0xff0000);
constexpr MenuColor caret = rgb(0xe0e0e0);
constexpr MenuColor accent = rgb(0xdd8795);
constexpr MenuColor control = rgb(0x886d85);
constexpr MenuColor focus = rgb(0xffffff);
// Main menu cards.
constexpr MenuColor mainButton = rgb(0x202020);
constexpr MenuColor mainButtonEnabled = rgb(0xffffff);
constexpr MenuColor mainButtonHover = rgb(0x808080);
constexpr MenuColor mainButtonEnabledHover = rgb(0xffffff);
constexpr MenuColor mainButtonOutline = rgb(0xffffff);
constexpr MenuColor mainButtonEnabledOutline = rgb(0xffffff);
// Settings buttons, switch tracks, and choices.
constexpr MenuColor settingsButton = rgb(0xffffff);
constexpr MenuColor settingsButtonEnabled = rgb(0xffffff);
constexpr MenuColor settingsButtonHover = rgb(0xffffff);
constexpr MenuColor settingsButtonEnabledHover = rgb(0xffffff);
constexpr MenuColor settingsButtonOutline = rgb(0xffffff);
constexpr MenuColor settingsButtonEnabledOutline = rgb(0xffffff);
constexpr MenuColor panelOutline = rgb(0x808080);
constexpr float titleTint = 0.7f;
constexpr float contentTint = 0.7f;
constexpr float buttonOpacity = 0.2f; // Normal button backgrounds.
constexpr float enabledButtonOpacity = 0.2f;
constexpr float hoverButtonOpacity = 0.2f;
constexpr float enabledHoverButtonOpacity = 0.3f;
constexpr float buttonOutlineOpacity = 0.15f; // Main tile outlines.
constexpr float settingsButtonOutlineOpacity = 0.4f;
constexpr float settingsDividerOpacity = 0.12f;
constexpr float creditOpacity = 0.45f;
constexpr float tileDividerOpacity = 0.15f;
constexpr float textBoxTint = 0.15f;

// Scrollbars (tiles and settings), sliders, and Tablist. Opacities multiply fades.
constexpr MenuColor scrollTrack = rgb(0xffffff);
constexpr MenuColor scrollThumb = rgb(0xffffff);
constexpr float scrollTrackOpacity = 0.1f;
constexpr float scrollThumbOpacity = 0.5f;
constexpr float scrollRadius = 0.0f; // Pixels.
constexpr MenuColor sliderTrack = rgb(0xffffff);
constexpr MenuColor sliderFill = rgb(0xffffff);
constexpr MenuColor sliderHandle = rgb(0xa0a0a0);
constexpr float sliderTrackOpacity = 0.1f;
constexpr float sliderFillOpacity = 0.5f;
constexpr float sliderHandleOpacity = 1.0f;
constexpr float sliderTrackRadiusPercent = 50.0f; // Percentage of track height.
constexpr float sliderHandleRadiusPercent = 25.0f; // Percentage of handle height.
constexpr MenuColor tablistBackground = rgb(0x101010);
constexpr MenuColor tablistOutline = rgb(0xffffff);
constexpr MenuColor tablistText = rgb(0xf0edf2);
constexpr MenuColor tablistBadge = rgb(0xff0000);
constexpr MenuColor tablistHeadPlaceholder = rgb(0x886d85);
constexpr float tablistBackgroundTint = 0.7f; // Mix dark tint into the blurred game.
constexpr float tablistBackgroundOpacity = 1.0f;
constexpr float tablistOutlineOpacity = 0.2f;
constexpr float tablistTextOpacity = 1.0f;
constexpr float tablistBadgeOpacity = 1.0f;
constexpr float tablistHeadOpacity = 1.0f;
constexpr float tablistHeadPlaceholderOpacity = 1.0f;
constexpr float tablistOutlineThickness = 1.5f; // Pixels; zero disables outline.
constexpr float tablistHeadPlaceholderRadius = 0.0f; // Pixels.
constexpr float tablistBlurScale = 6.0f;

// Corner radii are percentages of panel height; outline thicknesses are pixels.
constexpr float panelRadiusPercent = 2.0f;
constexpr float panelOutlineRadiusPercent = 1.6f;
constexpr float panelOutlineThickness = 1.5f;
constexpr float mainButtonOutlineRadiusPercent = 0.8f;
constexpr float mainButtonOutlineThickness = 1.5f;
constexpr float settingsButtonOutlineRadiusPercent = 1.5f;
constexpr float settingsButtonOutlineThickness = 1.5f; // Zero disables settings outlines.
constexpr float settingsChoiceRadiusPercent = 1.65f;
constexpr float switchRadiusPercent = 12.0f; // Percentage of track/thumb height.
constexpr float switchThumbOpacity = 0.25f;
constexpr float switchThumbHoverOpacity = 0.1f;

constexpr long long buttonTransitionNs = 180000000LL;
constexpr long long titleMotionDurationNs = 220000000LL;
constexpr long long settingsTransitionNs = 220000000LL;
constexpr long long caretMotionDurationNs = 160000000LL;
constexpr long long scrollDurationNs = 180000000LL;
constexpr long long animationDurationNs = 500000000LL;
constexpr long long settingsExitGridFadeDelayNs = 110000000LL;
constexpr long long openingCaptureDelayNs = animationDurationNs / 2;
constexpr int panelPaddingPercent = 8;
constexpr int contentTopPercent = 19;
constexpr int footerTopPercent = 91;
constexpr int rowStridePercent = 11;
constexpr int rowHeightPercent = 9;
constexpr int descriptionFontHeightPercent = 2;
constexpr int descriptionRowHeightPercent = 3;
constexpr int descriptionRowStridePercent = 4;
constexpr int textLineHeightPercent = 4;
}
