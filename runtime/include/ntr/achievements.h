// Local achievements: nothing online, one file per save. Shared by the game (checked when the
// game saves, shown in a popup and in the pause menu) and the launcher (the Achievements tab).
//
//   achievements/achievements.json      the definitions (shipped with the release)
//   achievements/icons/*.png            their icons
//   saves/<save>.achievements.json      for each save: every achievement, unlocked or not,
//                                       when it was unlocked and its icon
//
// A definition:
//   { "id": "chapter_02", "name": {"en": "...", "pt": "..."}, "description": {"en": "...", "pt": "..."},
//     "icon": "icons/chapter_02.png", "hidden": false,
//     "condition": { "source": "save", "field": "chapter", "op": ">=", "value": 2 } }
// A condition is one test on what the game's save holds, or "all" / "any" of a list of
// conditions. The fields (source "save", the only one so far): chapter, level (the team's),
// members, play_hours, has_unit (value: a unit number) and saved (true once the game was
// saved). An unlocked achievement stays unlocked whatever the save holds later.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <ntr/json.h>
#include <ntr/savedata.h>

namespace ntr {

// What the conditions test. From the save for now; live state (RAM) can fill it later.
struct GameFacts {
    bool saved = false;  // a game saved in it (the rest is only meaningful then)
    int chapter = 0, level = 0, members = 0;
    uint32_t play_frames = 0;    // 60 a second
    std::vector<uint16_t> units;  // unit numbers in the team's roster
};
GameFacts game_facts(const SaveSummary& save);

struct Achievement {
    std::string id;
    std::string name_en, name_pt, description_en, description_pt;
    std::string icon;  // relative to achievements/
    bool hidden = false;  // "???" until unlocked
    Json condition;
    // In the language of the launcher and of the pause menu.
    const std::string& name() const;
    const std::string& description() const;
};

struct AchievementStatus {
    bool unlocked = false;
    std::string unlocked_at;  // ISO 8601, local time with its offset ("2026-10-29T14:03:12-03:00")
};

class Achievements {
public:
    // <root>/achievements/achievements.json. False with a message; bad definitions are left
    // out and logged, the others kept.
    bool load_definitions(const std::string& root, std::string& error);
    const std::vector<Achievement>& list() const { return list_; }
    std::string icon_path(const Achievement& a) const;  // on disk

    // The save's own file: "saves/x.sav" -> "saves/x.achievements.json".
    static std::string state_path(const std::string& save_path);
    // A missing file is a save with nothing unlocked yet.
    void load_state(const std::string& save_path);
    bool save_state(std::string& error) const;  // written whole (temporary file, then rename)
    const std::string& save_path() const { return save_path_; }
    const AchievementStatus& status(const std::string& id) const;
    int unlocked_count() const;

    // Unlocks what `facts` satisfy; the indices (in list()) of those unlocked now.
    std::vector<size_t> update(const GameFacts& facts);

private:
    std::string root_;
    std::vector<Achievement> list_;
    std::string save_path_;
    std::vector<std::pair<std::string, AchievementStatus>> state_;  // by id, in the file's order
};

// False with a message when a condition cannot be read; `ok` then says nothing.
bool achievement_condition(const Json& condition, const GameFacts& facts, bool& ok, std::string& error);
std::string local_timestamp();  // now, as AchievementStatus::unlocked_at

}  // namespace ntr
