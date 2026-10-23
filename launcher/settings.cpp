// The settings tab: the shared settings panel (ntr-ui) over boej.ini.
#include <ntr/ui.h>

#include "launcher.h"

namespace launcher {

namespace {

ntr::Config g_config;

}  // namespace

void settings_load() { ntr::config_load(paths().config(), g_config); }

const ntr::Config& settings() { return g_config; }

bool settings_capturing() { return ntr::ui::capturing(); }

void settings_event(const SDL_Event& ev) { ntr::ui::capture_event(ev); }

void settings_ui() { ntr::ui::settings_panel(g_config, paths().config(), {}); }

}  // namespace launcher
