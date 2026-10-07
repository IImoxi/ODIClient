#pragma once

void client_settings_get_zoom(bool* enabled, int* key, int* defaultLevel, int* scrollStep);
void client_settings_set_zoom(bool enabled, int key, int defaultLevel, int scrollStep);

// Shared odiclient.conf settings. The existing AutoGG adapter owns migration and
// atomic file saves; these calls only copy values under its short settings lock.
void client_settings_get_modules(bool* sprint, bool* blur, int* strength);
void client_settings_set_modules(bool sprint, bool blur, int strength);
void client_settings_get_fps_limit(bool* enabled, int* limit);
void client_settings_set_fps_limit(bool enabled, int limit);
void client_settings_get_blur(bool* fpsAverage, int* averageHz, bool* screenBlur);
void client_settings_set_blur(bool fpsAverage, int averageHz, bool screenBlur);

void client_settings_set_center_cursor(bool value);
bool client_settings_get_center_cursor();

struct RenderSettings {
    bool enabled = false, below = false, above = false;
    int belowDistance = 64, aboveDistance = 128;
    bool horizontal = false;
    int radius = 128;
};
RenderSettings client_settings_get_render();
void client_settings_set_render(const RenderSettings& settings);

void client_settings_set_tablist(bool value);
bool client_settings_get_tablist();

void client_settings_set_particles(bool value);
bool client_settings_get_particles();

struct ChatModsSettings {
    bool enabled = false, blacklist = false;
    char keywords[256]{};
};
ChatModsSettings client_settings_get_chat_mods();
void client_settings_set_chat_mods(const ChatModsSettings& settings);

struct LobbyWatchSettings {
    bool enabled = false;
    char rules[256]{};
};
LobbyWatchSettings client_settings_get_lobby_watch();
void client_settings_set_lobby_watch(const LobbyWatchSettings& settings);

struct CCUtilsSettings {
    bool enabled = false, partyInvites = false, playerPing = false;
};
CCUtilsSettings client_settings_get_cc_utils();
void client_settings_set_cc_utils(CCUtilsSettings settings);

void client_settings_set_tablist_mojangles(bool value);
bool client_settings_get_tablist_mojangles();

struct FpsDisplaySettings {
    bool enabled = false, low = false;
    int intervalMs = 1000;
    int fontScale = 2; // ui_scale tier index; 2 = 1x.
    int anchor = 0; // DisplayAnchor order: TL, TR, BL, BR.
};
FpsDisplaySettings client_settings_get_fps_display();
void client_settings_set_fps_display(FpsDisplaySettings settings);

struct EnvironmentSettings {
    bool enabled = false, time = false, fog = false;
    int ticks = 6000, hue = 0, saturation = 0, value = 100;
    bool sky = false, clouds = true, vanillaCelestials = false; // clouds: legacy config slot, ignored by renderer.
    // Legacy config slots: preserved for compatibility, renderer always optimizes.
    bool skyLookup = false, skyHalfResolution = false, skyReducedSamples = false;
};
EnvironmentSettings client_settings_get_environment();
void client_settings_set_environment(EnvironmentSettings settings);
