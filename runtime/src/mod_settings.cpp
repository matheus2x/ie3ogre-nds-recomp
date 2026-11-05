// The options a mod offers the player (ntr/mod_settings.h).
#include <ntr/json.h>
#include <ntr/mod_settings.h>

namespace ntr {

namespace {

ModText text_of(const Json& v) {
    ModText t;
    if (v.is_string()) {
        t.en = t.pt = v.as_string();
    } else if (v.is_object()) {
        t.en = v.get("en").as_string();
        t.pt = v.get("pt").as_string();
    }
    return t;
}

std::string key_of(const std::string& mod, const std::string& id) { return mod + " / " + id; }

}  // namespace

const std::string& ModText::get() const {
    const std::string& first = language() == Language::Portuguese ? pt : en;
    return first.empty() ? (&first == &pt ? en : pt) : first;
}

bool mod_settings_parse(const std::string& text, std::vector<ModSetting>& out, std::string& error) {
    out.clear();
    Json doc;
    if (!json_parse(text, doc, error)) return false;
    for (const Json& s : doc.get("settings").items()) {
        ModSetting m;
        m.id = s.get("id").as_string();
        m.label = text_of(s.get("label"));
        m.hint = text_of(s.get("hint"));
        for (const Json& c : s.get("options").items()) m.choices.push_back({c.get("id").as_string(), text_of(c.get("label"))});
        m.fallback = s.get("default").as_string();
        if (m.id.empty() || m.id.find_first_of("/=[]\n") != std::string::npos || m.choices.empty()) {
            error = L("an option without an id or choices", "uma opção sem id ou sem escolhas");
            return false;
        }
        bool known = false;
        for (const ModSetting::Choice& c : m.choices) {
            if (c.id.empty()) {
                error = m.id + L(": a choice without an id", ": uma escolha sem id");
                return false;
            }
            known |= c.id == m.fallback;
        }
        if (!known) m.fallback = m.choices.front().id;
        out.push_back(std::move(m));
    }
    return true;
}

std::string mod_setting_get(const Config& config, const std::string& mod, const ModSetting& setting) {
    std::string v = config.get(kModSettingsSection, key_of(mod, setting.id));
    for (const ModSetting::Choice& c : setting.choices) {
        if (c.id == v) return v;
    }
    return setting.fallback;
}

void mod_setting_set(Config& config, const std::string& mod, const std::string& id, const std::string& choice) {
    config.set(kModSettingsSection, key_of(mod, id), choice);
}

}  // namespace ntr
