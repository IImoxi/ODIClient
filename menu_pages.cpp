#include "custom_menu.h"
#include "client_modules.h"
#include "client_settings.h"
#include "auto_gg.h"
#include "chat.h"
#include "player_target.h"
#include "analog_input.h"
#include "zoom.h"
#include "render.h"
#include "environment.h"
#include "sky_renderer.h"
#include "tablist.h"
#include "particles.h"
#include "ui_scale.h"
#include "projection_jitter.h"

namespace {
void zoomMultiplier(int tenths, char* value) {
    int n = 0, whole = tenths / 10;
    if (whole >= 10) value[n++] = static_cast<char>('0' + whole / 10);
    value[n++] = static_cast<char>('0' + whole % 10);
    if (tenths % 10) {
        value[n++] = '.';
        value[n++] = static_cast<char>('0' + tenths % 10);
    }
    value[n++] = 'x'; value[n] = 0;
}
void fontScaleLabel(int index, char* value) {
    const char* text = ui_scale::labels[index];
    while (*text) *value++ = *text++;
    *value = 0;
}
bool trailMode() { return !client_blur_average(); }
void jitterSamplesLabel(int mode, char* value) {
    value[0] = static_cast<char>('0' + (2 << mode));
    value[1] = 0;
}
bool cloudsVisible() { return client_environment_sky() && client_environment_clouds(); }
void cloudDetailLabel(int level, char* value) {
    const char* text = level == 1 ? "Low" : level == 2 ? "Medium" : "High";
    while (*text) *value++ = *text++;
    *value = 0;
}
void cloudResolutionLabel(int level, char* value) {
    const char* text = level == 0 ? "Quarter" : level == 1 ? "Half" : "Full";
    while (*text) *value++ = *text++;
    *value = 0;
}
}

void declare_menu_pages() {
    MenuPage jitterPage = newPage("Jitter Anti-Aliasing");
    jitterPage.toggle("Enable Jitter AA", client_set_jitter, client_jitter_enabled)
        .slider("Samples", 0, 2, client_set_jitter_sample_mode,
                client_jitter_sample_mode, jitterSamplesLabel).whenEnabled()
        .text("2 samples: Recommended 120 Hz+ and 120 FPS+")
            .whenEnabled([] { return client_jitter_sample_mode() == 0; })
        .text("4 samples: Recommended 165 Hz+ and 165 FPS+")
            .whenEnabled([] { return client_jitter_sample_mode() == 1; })
        .text("8 samples: Recommended 360 Hz+ and 360 FPS+")
            .whenEnabled([] { return client_jitter_sample_mode() == 2; })
        .text("Based on owner testing to avoid visible flicker.").whenEnabled()
        .text("No frame blending. Resets after restart.");
    if (projection_jitter_error()) jitterPage.text(projection_jitter_error());
    newTile("Jitter Anti-Aliasing").opens(jitterPage).onToggle(client_set_jitter, client_jitter_enabled);

    MenuPage tablistPage = newPage("Tablist");
    tablistPage.toggle("Enable Tablist", client_set_tablist, client_tablist_enabled)
        .dropdown("Font", "Inter", "Mojangles", client_settings_set_tablist_mojangles,
                  client_settings_get_tablist_mojangles).whenEnabled();
    if (tablist_error()) tablistPage.text(tablist_error());
    newTile("Tablist").opens(tablistPage).onToggle(client_set_tablist, client_tablist_enabled);

    MenuPage particlesPage = newPage("Particles");
    particlesPage.toggle("Enable Particles", client_set_particles, client_particles_enabled)
        .text("Critical-hit particles when you attack a player.");
    if (particles_error()) particlesPage.text(particles_error());
    newTile("Particles").icon("assets/icon-particles.png").opens(particlesPage).onToggle(client_set_particles, client_particles_enabled);

    MenuPage zoomPage = newPage("Zoom");
    zoomPage.toggle("Enable Zoom", client_set_zoom, client_zoom_enabled)
        .keyBind("Hold to zoom", client_set_zoom_key, client_zoom_key).whenEnabled()
        .slider("Default zoom", 15, 300, client_set_zoom_default, client_zoom_default, zoomMultiplier).whenEnabled()
        .slider("Scroll step", 1, 50, client_set_zoom_scroll, client_zoom_scroll, zoomMultiplier).whenEnabled()
        .text("Hold key and scroll to adjust camera zoom.");
    if (zoom_status()) zoomPage.text(zoom_status());
    newTile("Zoom")
        .icon("assets/icon-zoom.png")
        .opens(zoomPage)
        .onToggle(client_set_zoom, client_zoom_enabled);

    MenuPage blurPage = newPage("Motion Blur");
    blurPage.toggle("Enable Motion Blur", client_set_blur, client_blur_enabled)
        .choice("Mode", "Trail", "FPS average", client_set_blur_average, client_blur_average).whenEnabled()
        .slider("Strength (%)", 0, 80, client_set_blur_strength, client_blur_strength).whenEnabled(trailMode)
        .slider("Target Hz", 30, 500,
                client_set_blur_average_hz, client_blur_average_hz).whenEnabled(client_blur_average);
    newTile("Motion Blur")
        .opens(blurPage)
        .icon("assets/icon-motionblur.png")
        .onToggle(client_set_blur, client_blur_enabled);

    MenuPage fpsDisplayPage = newPage("FPS Display");
    fpsDisplayPage.toggle("Enable FPS Display", client_set_fps_display, client_fps_display_enabled)
        .toggle("Show 1% low", client_set_fps_low, client_fps_low).whenEnabled()
        .slider("Average / update (ms)", 250, 2000, client_set_fps_interval,
                client_fps_interval, nullptr, 250).whenEnabled()
        .dropdown("Font", "Inter", "Mojangles",
                  [](bool value) { auto settings = client_settings_get_fps_display(); settings.mojangles = value; client_settings_set_fps_display(settings); },
                  []() { return client_settings_get_fps_display().mojangles; }).whenEnabled()
        .slider("Font scale", 0, ui_scale::count - 1, client_set_fps_font_scale,
                client_fps_font_scale, fontScaleLabel).whenEnabled()
        .anchor("Anchor", client_set_fps_anchor, client_fps_anchor).whenEnabled()
        .text("1% low: average of the slowest 1% of frames.");
    newTile("FPS Display").opens(fpsDisplayPage).onToggle(client_set_fps_display, client_fps_display_enabled);

    MenuPage fpsPage = newPage("FPS Limiter");
    fpsPage.toggle("Enable FPS Limiter", client_set_fps_limit, client_fps_limit_enabled)
        .slider("FPS Limit", 30, 480, client_set_fps_value, client_fps_value).whenEnabled()
        .toggle("Reduce input delay", client_set_fps_native, client_fps_native).whenEnabled()
        .text("Applies the FPS limit before the next frame.").whenEnabled(client_fps_native)
        .text("Unavailable; restart Minecraft if just enabled.").whenEnabled(client_fps_native_unavailable);
    newTile("FPS Limiter")
        .opens(fpsPage)
        .icon("assets/icon-fpslimiter.png")
        .onToggle(client_set_fps_limit, client_fps_limit_enabled);

    MenuPage renderPage = newPage("Render");
    renderPage.toggle("Render distance", client_set_render, client_render_enabled)
        .toggle("Horizontal", client_set_render_horizontal, client_render_horizontal).whenEnabled()
        .slider("Horizontal", 16, 256, client_set_render_radius,
                client_render_radius).whenEnabled(client_render_horizontal).groupWithPrevious()
        .toggle("Below", client_set_render_below, client_render_below).whenEnabled()
        .slider("Below", 16, 256, client_set_render_below_distance,
                client_render_below_distance).whenEnabled(client_render_below).groupWithPrevious()
        .toggle("Above", client_set_render_above, client_render_above).whenEnabled()
        .slider("Above", 16, 256, client_set_render_above_distance,
                client_render_above_distance).whenEnabled(client_render_above).groupWithPrevious()
        .text("Experimental terrain distance limits.")
        .text("Can hide visible cliffs or cave openings.");
    if (render_error()) renderPage.text(render_error());
    newTile("Render").opens(renderPage).onToggle(client_set_render, client_render_enabled);

    MenuPage environmentPage = newPage("Environment");
    environmentPage.toggle("Enable Environment", client_set_environment, client_environment_enabled)
        .toggle("Physically inspired sky", client_set_environment_sky, client_environment_sky).whenEnabled()
        .toggle("Quarter resolution", client_set_environment_sky_quarter_resolution, client_environment_sky_quarter_resolution)
            .whenEnabled(client_environment_sky).groupWithPrevious()
        .toggle("Vanilla sun/moon", client_set_environment_vanilla_celestials, client_environment_vanilla_celestials)
            .whenEnabled(client_environment_sky).groupWithPrevious()
        .toggle("Clouds", client_set_environment_clouds, client_environment_clouds)
            .whenEnabled(client_environment_sky).groupWithPrevious()
        .slider("Cloud detail", 1, 3, client_set_environment_cloud_detail, client_environment_cloud_detail, cloudDetailLabel)
            .whenEnabled(cloudsVisible).groupWithPrevious()
        .slider("Cloud samples", 8, 64, client_set_environment_cloud_samples, client_environment_cloud_samples, nullptr, 8)
            .whenEnabled(cloudsVisible).groupWithPrevious()
        .slider("Cloud resolution", 0, 2, client_set_environment_cloud_resolution, client_environment_cloud_resolution, cloudResolutionLabel)
            .whenEnabled(cloudsVisible).groupWithPrevious()
        .toggle("Time changer", client_set_environment_time, client_environment_time).whenEnabled()
        .slider("Time (ticks)", 0, 23999, client_set_environment_ticks,
                client_environment_ticks).whenEnabled(client_environment_time).groupWithPrevious()
        .toggle("Weather changer", client_set_environment_weather, client_environment_weather).whenEnabled()
        .slider("Weather (clear / rain / thunder)", 0, 100, client_set_environment_weather_amount,
                client_environment_weather_amount, nullptr, 10).whenEnabled(client_environment_weather).groupWithPrevious()
        .toggle("Fog color", client_set_environment_fog, client_environment_fog).whenEnabled()
        .slider("Hue (degrees)", 0, 360, client_set_environment_hue,
                client_environment_hue).whenEnabled(client_environment_fog).groupWithPrevious()
        .slider("Saturation (%)", 0, 100, client_set_environment_saturation,
                client_environment_saturation).whenEnabled(client_environment_fog)
        .slider("Value (%)", 0, 100, client_set_environment_value,
                client_environment_value).whenEnabled(client_environment_fog);
    if (environment_error()) environmentPage.text(environment_error());
    else if (sky_renderer_error()) environmentPage.text(sky_renderer_error());
    else environmentPage.text(sky_renderer_status());
    newTile("Environment").opens(environmentPage)
        .onToggle(client_set_environment, client_environment_enabled);

    MenuPage sprintPage = newPage("Auto Sprint");
    sprintPage.toggle("Enable Auto Sprint", client_set_sprint, client_sprint_enabled);
    newTile("Auto Sprint")
        .opens(sprintPage)
        .onToggle(client_set_sprint, client_sprint_enabled);

    MenuPage cursorPage = newPage("Center Cursor");
    cursorPage.toggle("Enable Center Cursor", client_set_center_cursor, client_center_cursor_enabled)
        .text("Center the pointer when a GUI releases mouse capture.");
    newTile("Center Cursor").icon("assets/icon-cursor.png").opens(cursorPage)
        .onToggle(client_set_center_cursor, client_center_cursor_enabled);

    MenuPage analogPage = newPage("Analog WASD");
    analogPage.toggle("Enable Analog WASD", client_set_analog, client_analog_enabled);
    analogPage.text(analog_input_supported()
        ? "Requires the NuPhy analog helper."
        : "Analog WASD is unavailable on this system.");
    newTile("Analog WASD")
        .opens(analogPage)
        .onToggle(client_set_analog, client_analog_enabled);

    auto chatSettings = client_settings_get_chat_mods();
    MenuPage chatPage = newPage("Chat mods");
    chatPage.toggle("Enable Chat mods", client_set_chat_mods, client_chat_mods_enabled)
        .toggle("Message blacklist", client_set_message_blacklist, client_message_blacklist)
        .textBox("Keywords", chatSettings.keywords, client_set_chat_keywords, true, true).whenEnabled()
        .text("Comma-separated keywords; Shift+Enter: new line.")
        .text("Matching incoming messages are hidden.");
    newTile("Chat mods").opens(chatPage).onToggle(client_set_chat_mods, client_chat_mods_enabled);

    auto lobbyWatch = client_settings_get_lobby_watch();
    MenuPage lobbyPage = newPage("Lobby Scanner");
    lobbyPage.text("Run a command when a listed player joins the lobby.")
        .toggle("Enable Lobby Scanner", client_set_lobby_watch, client_lobby_watch_enabled)
        .textBox("Players / commands", lobbyWatch.rules, client_set_lobby_watch_rules, false, true).whenEnabled()
        .text("steve/hub runs a command; steve#/hub sends chat.")
        .text("steve?/hub asks; steve?#/hub asks to send chat.");
    newTile("Lobby Scanner").opens(lobbyPage).onToggle(client_set_lobby_watch, client_lobby_watch_enabled);

    MenuPage ccUtilsPage = newPage("CC Utils");
    ccUtilsPage.toggle("Enable CC Utils", client_set_cc_utils, client_cc_utils_enabled)
        .toggle("Custom party invites dialog", client_set_party_invites, client_party_invites_enabled).whenEnabled()
        .text("Ask before accepting party invites from chat.").whenEnabled()
        .toggle("Player ping", client_set_player_ping, client_player_ping_enabled).whenEnabled()
        .text("Shift + right click a player; 3 second cooldown.").whenEnabled();
    if (chat_error()) ccUtilsPage.text(chat_error());
    else if (player_target_error()) ccUtilsPage.text(player_target_error());
    newTile("CC Utils").opens(ccUtilsPage).onToggle(client_set_cc_utils, client_cc_utils_enabled);

    char trigger[256], response[256];
    auto_gg_get_text(trigger, response);
    MenuPage autoGGPage = newPage("AutoGG");
    autoGGPage.text("Automatic replies to matching chat.")
        .toggle("Enable AutoGG", client_set_auto_gg, client_auto_gg_enabled)
        .textBox("Trigger", trigger, auto_gg_set_trigger).whenEnabled()
        .textBox("Text", response, auto_gg_set_response).whenEnabled();
    newTile("AutoGG")
        .icon("assets/icon-autogg.png")
        .opens(autoGGPage)
        .onToggle(client_set_auto_gg, client_auto_gg_enabled);
}
