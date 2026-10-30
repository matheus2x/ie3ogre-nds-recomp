// The achievements while the game runs (ntr/achievements.h). They are checked against the
// game's own save: once at boot (a save that already has what an achievement asks, or
// achievements added by a newer release) and after each save. The same reading of the save
// feeds the Discord presence (discord.cpp). One save is many backup
// writes over a few frames, so the check waits until the writes stop. The save is not part
// of savestates, so loading one or rewinding never takes an achievement back or gives one.
#include <ntr/runtime.h>

#include "runtime.h"

namespace ntr {

namespace {

constexpr int kQuietFrames = 30;  // frames without a backup write: the save is complete

Achievements g_achievements;
bool g_enabled = false;
bool g_pending = false;
int g_quiet = 0;
std::deque<size_t> g_announce;  // indices in g_achievements.list()

void check(const char* why) {
    g_pending = false;
    const std::vector<uint8_t>& data = card_save_data();
    SaveSummary save = read_save_data(data.data(), data.size());
    discord_save_changed(save);  // the chapter and level on the profile, achievements or not
    if (!g_enabled) return;
    std::vector<size_t> unlocked = g_achievements.update(game_facts(save));
    if (unlocked.empty()) {
        log_detail("[achievements] checked (%s): nothing new\n", why);
        return;
    }
    std::string error;
    if (!g_achievements.save_state(error)) NTR_LOG("[achievements] %s\n", error.c_str());
    for (size_t i : unlocked) {
        const Achievement& a = g_achievements.list()[i];
        NTR_LOG("[achievements] unlocked %s (%s)\n", a.id.c_str(), why);
        session_event("conquista desbloqueada: %s", a.id.c_str());
        g_announce.push_back(i);
    }
}

}  // namespace

void achievements_init(const std::string& save_path) {
    std::string error;
    if (!g_achievements.load_definitions(host_base_path(), error)) {
        NTR_LOG("[achievements] off: %s\n", error.c_str());
        check("the save at boot");  // for the Discord presence only
        return;
    }
    g_achievements.load_state(save_path);
    g_enabled = true;
    NTR_LOG("[achievements] %zu defined, %d unlocked in %s\n", g_achievements.list().size(),
            g_achievements.unlocked_count(), Achievements::state_path(save_path).c_str());
    check("the save at boot");
}

void achievements_game_saved() {
    g_pending = true;
    g_quiet = 0;
}

void achievements_frame() {
    if (g_pending && ++g_quiet >= kQuietFrames) check("the game saved");
}

void achievements_flush() {
    if (g_pending) check("the game saved");
}

const Achievements& achievements() { return g_achievements; }

const Achievement* achievements_take_unlocked() {
    if (g_announce.empty()) return nullptr;
    size_t i = g_announce.front();
    g_announce.pop_front();
    return &g_achievements.list()[i];
}

}  // namespace ntr
