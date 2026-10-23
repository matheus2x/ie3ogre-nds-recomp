// Shared Dear ImGui screens (the ntr-ui library): the settings panel, used by the
// launcher's settings tab and by the game's pause menu. ImGui stays out of this header.
#pragma once

#include <functional>
#include <string>

#include <SDL3/SDL.h>

#include <ntr/common.h>

namespace ntr::ui {

// ImGui context, style and a system font for this window, scaled by `scale`.
void init(SDL_Window* window, SDL_Renderer* renderer, float scale);
void shutdown();
void process_event(const SDL_Event& ev);
void begin_frame();
void end_frame(SDL_Renderer* renderer);  // draws the UI over what is already rendered

struct SettingsOptions {
    bool in_game = false;              // the pause menu: no launcher-only options
    std::function<void()> on_change;   // after each change is saved
    // Extra items in the side list: above the sections, and pinned to its bottom
    // (nav_bottom_height rows tall).
    std::function<void()> nav_top;
    std::function<void()> nav_bottom;
    float nav_bottom_height = 0;
};

// The settings: a side list of sections (video, audio, controls...) and the chosen one
// beside it, filling the space left in the current window. Every change is saved to `path` at
// once and then reported through on_change.
void settings_panel(Config& config, const std::string& path, const SettingsOptions& options);
// Waiting for a key or gamepad input to bind: those events go to capture_event instead.
bool capturing();
void capture_event(const SDL_Event& ev);

enum class PauseAction { None, Resume, Restart, Quit };
// The game's pause menu, settings included; one frame of it.
PauseAction pause_menu(Config& config, const std::string& path, const SettingsOptions& options);

}  // namespace ntr::ui
