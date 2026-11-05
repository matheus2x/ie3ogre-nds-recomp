// The options a mod offers the player (settings.json at the root of the mod, next to
// mod.ini; docs/modding/MODS.md, "Mod options"), shown in the settings' Mods page of the
// launcher and of the pause menu. The choices go to boej.ini, [mod_settings], one key per
// mod and option: "<the mod's name in mod.ini> / <option id> = <choice id>", so a new version
// of the mod keeps them.
//
//   {"settings": [{"id": "hd_font", "label": {"en": "Text font", "pt": "Fonte do texto"},
//                  "hint": {...}, "default": "default",
//                  "options": [{"id": "default", "label": {...}}, {"id": "atkinson", "label": "Atkinson"}]}]}
//
// A label or hint is a string (any language) or {"en": ..., "pt": ...}. What an option does is
// up to the port: "hd_font" picks the HD text font (hd/fonts.json; "default" is the game's).
#pragma once

#include <string>
#include <vector>

#include <ntr/common.h>

namespace ntr {

struct ModText {
    std::string en, pt;
    const std::string& get() const;  // in the current language (ntr::language()), else the other
};

struct ModSetting {
    struct Choice {
        std::string id;
        ModText label;
    };
    std::string id;
    ModText label, hint;
    std::vector<Choice> choices;
    std::string fallback;  // "default" in the JSON: the choice when the player made none
};

struct ModSettings {
    std::string name;     // [mod] name of mod.ini (the key in boej.ini)
    std::string version;  // for the page
    bool enabled = true;  // on in [mods] enabled (the launcher also lists the ones off)
    std::vector<ModSetting> settings;
};

constexpr const char* kModSettingsFile = "settings.json";
constexpr const char* kModSettingsSection = "mod_settings";

// The settings of a settings.json; false with a message for the player.
bool mod_settings_parse(const std::string& text, std::vector<ModSetting>& out, std::string& error);
// The player's choice of a mod's option (a known choice, else the option's default), and
// setting it.
std::string mod_setting_get(const Config& config, const std::string& mod, const ModSetting& setting);
void mod_setting_set(Config& config, const std::string& mod, const std::string& id, const std::string& choice);

}  // namespace ntr
