// Shared Dear ImGui screens (the ntr-ui library): the settings panel, used by the
// launcher's settings tab and by the game's pause menu. ImGui stays out of this header.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include <ntr/common.h>

struct ImFont;

namespace ntr {
class Achievements;
struct Achievement;
}  // namespace ntr

namespace ntr::ui {

// The palette, from the game's slot card (pic2d/menu/MMSlot.SPF_) and its logo, as ImU32
// (0xAABBGGRR): the card's blue, the lightning's yellow and the Ogre's magenta.
constexpr uint32_t kBackground = 0xFF1F0E0B;     // #0B0E1F
constexpr uint32_t kPrimary = 0xFFE67331;        // #3173E6, the card's body
constexpr uint32_t kPrimaryHover = 0xFFF0884A;   // #4A88F0
constexpr uint32_t kPrimaryActive = 0xFFAC4A10;  // #104AAC, the card's dark blue
constexpr uint32_t kHighlight = 0xFF1FD2FF;      // #FFD21F
constexpr uint32_t kMagenta = 0xFFF03CD6;        // #D63CF0
constexpr uint32_t kSuccess = 0xFF7ADB5B;        // #5BDB7A
constexpr uint32_t kError = 0xFF3C5AFF;          // #FF5A3C

// ImGui context, style and fonts (Rubik, embedded) for this window, scaled by `scale`.
void init(SDL_Window* window, SDL_Renderer* renderer, float scale);
ImFont* bold_font();  // for titles; the regular one is the default font
void shutdown();
void process_event(const SDL_Event& ev);
void begin_frame();
void end_frame(SDL_Renderer* renderer);  // draws the UI over what is already rendered
// A frame over the game's picture while the renderer keeps its logical presentation: ImGui
// draws in output pixels all the same (sharp text), whatever the logical size.
void begin_overlay_frame(SDL_Renderer* renderer);
void end_overlay_frame(SDL_Renderer* renderer);

struct SettingsOptions {
    bool in_game = false;              // the pause menu: no launcher-only options
    std::function<void()> on_change;   // after each change is saved
    // Extra items in the side list: above the sections, and pinned to its bottom
    // (nav_bottom_height rows tall).
    std::function<void()> nav_top;
    std::function<void()> nav_bottom;
    float nav_bottom_height = 0;
    // Pages of the caller's own, first in the side list (the pause menu's savestates and
    // achievements).
    struct Page {
        std::string name;
        std::function<void()> draw;
    };
    std::vector<Page> extra_pages;
};

// The settings: a side list of sections (video, audio, controls...) and the chosen one
// beside it, filling the space left in the current window. Every change is saved to `path` at
// once and then reported through on_change.
void settings_panel(Config& config, const std::string& path, const SettingsOptions& options);
// Waiting for a key or gamepad input to bind: those events go to capture_event instead.
bool capturing();
void capture_event(const SDL_Event& ev);

enum class PauseAction { None, Resume, Restart, Quit, SaveState, LoadState };
// The game's pause menu, settings included; one frame of it. `states` says what each
// savestate slot holds ("" = empty, else when it was saved); for SaveState and LoadState
// `slot` (1 = the first) is the one chosen. `loadable` tells the slots that can be loaded.
PauseAction pause_menu(Config& config, const std::string& path, const SettingsOptions& options,
                       const std::vector<std::string>& states, const std::vector<bool>& loadable, int& slot);

// --- achievements (ntr/achievements.h) ----------------------------------------------------

// A PNG as a texture of `renderer`, loaded once; nullptr when it cannot be read.
SDL_Texture* png_texture(SDL_Renderer* renderer, const std::string& path);
// Every achievement with its icon, name, description and when it was unlocked; the
// unlocked ones first. For the pause menu's page and the launcher's tab.
void achievements_list(const Achievements& achievements, SDL_Renderer* renderer);
// The "achievement unlocked" box in the top corner of an overlay frame; `shown` goes from 0
// (hidden) to 1 (fully in).
void achievement_popup(const Achievement& achievement, SDL_Renderer* renderer, const std::string& icon_path, float shown);

}  // namespace ntr::ui
