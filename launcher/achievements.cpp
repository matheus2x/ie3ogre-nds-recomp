// The Achievements tab: the local achievements of a save (ntr/achievements.h), the one the
// game uses at first. Its file (saves/<save>.achievements.json) is written by the game when
// it saves, and read again here when it changes. "Check this save" unlocks what the save
// already has, for saves from before the achievements or from another emulator.
#include <SDL3/SDL.h>
#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <system_error>

#include <ntr/achievements.h>
#include <ntr/savedata.h>
#include <ntr/ui.h>

#include "launcher.h"

namespace fs = std::filesystem;

namespace launcher {

namespace {

ntr::Achievements g_achievements;
bool g_loaded = false;
std::string g_error;          // the definitions could not be read
std::string g_file;           // the save shown, a name in saves/
std::string g_stamp;          // its achievements file's size and time, to notice the game's writes
std::vector<std::string> g_files;  // the .sav files in saves/
double g_scan_time = -10;
bool g_game_running = false;  // at the last scan (looking for the process every frame costs)
std::string g_status;

std::string stamp_of(const std::string& path) {
    std::error_code ec;
    fs::path p = ntr::fs_path(path);
    if (!fs::exists(p, ec)) return "-";
    return std::to_string(fs::file_size(p, ec)) + ":" + std::to_string(int64_t(fs::last_write_time(p, ec).time_since_epoch().count()));
}

void scan() {
    g_files.clear();
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(ntr::fs_path(paths().saves()), ec)) {
        std::string name = ntr::utf8_string(entry.path().filename());
        std::string ext = ntr::utf8_string(entry.path().extension());
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        if (entry.is_regular_file(ec) && ext == ".sav" && name[0] != '.') g_files.push_back(name);
    }
    std::sort(g_files.begin(), g_files.end());
}

void show(const std::string& file) {
    g_file = file;
    std::string save = paths().saves() + file;
    g_achievements.load_state(save);
    g_stamp = stamp_of(ntr::Achievements::state_path(save));
}

// The save's own data against the achievements; what it unlocks is written to its file.
void check_save() {
    std::string save = paths().saves() + g_file;
    std::vector<uint8_t> data(ntr::kSaveDataSize);
    size_t got = 0;
    if (FILE* f = ntr::open_file(ntr::fs_path(save), "rb")) {
        got = std::fread(data.data(), 1, data.size(), f);
        std::fclose(f);
    }
    ntr::GameFacts facts = ntr::game_facts(ntr::read_save_data(data.data(), got));
    if (!facts.saved) {
        g_status = L("This save has no game saved in it yet.", "Este save ainda não tem um jogo salvo.");
        return;
    }
    std::vector<size_t> unlocked = g_achievements.update(facts);
    std::string error;
    if (!unlocked.empty() && !g_achievements.save_state(error)) {
        NTR_LOG("[launcher] %s\n", error.c_str());
        g_status = error;
        return;
    }
    g_stamp = stamp_of(ntr::Achievements::state_path(save));
    char text[128];
    std::snprintf(text, sizeof text, L("%zu achievement(s) unlocked.", "%zu conquista(s) desbloqueada(s)."), unlocked.size());
    g_status = unlocked.empty() ? L("Nothing new in this save.", "Nada de novo neste save.") : text;
}

}  // namespace

void achievements_tab(SDL_Renderer* renderer) {
    if (!g_loaded) {
        g_loaded = true;
        if (!g_achievements.load_definitions(paths().root, g_error)) NTR_LOG("[launcher] achievements: %s\n", g_error.c_str());
    }
    if (ImGui::GetTime() - g_scan_time > 1.5) {
        g_scan_time = ImGui::GetTime();
        g_game_running = game_process_running();
        scan();
        // The save in use first; another one when that one is gone (renamed by the launcher).
        if (g_file.empty() || std::find(g_files.begin(), g_files.end(), g_file) == g_files.end()) {
            std::string current = current_save();
            bool found = std::find(g_files.begin(), g_files.end(), current) != g_files.end();
            show(found || g_files.empty() ? current : g_files.front());
        } else if (stamp_of(ntr::Achievements::state_path(paths().saves() + g_file)) != g_stamp) {
            show(g_file);  // the game wrote it
        }
    }

    ImGui::TextWrapped("%s", L("Achievements are local: each save has its own, in a .achievements.json file next to the .sav "
                               "in the saves folder. The game checks them every time it saves.",
                               "As conquistas são locais: cada save tem as suas, num arquivo .achievements.json ao lado do "
                               ".sav na pasta saves. O jogo confere a cada vez que salva."));
    ImGui::Spacing();
    if (!g_error.empty()) {
        ImGui::TextColored(ImVec4(1, 0.45f, 0.4f, 1), "%s", g_error.c_str());
        return;
    }
    if (g_files.empty()) {
        ImGui::TextDisabled("%s", L("No saves yet (see Saves).", "Nenhum save ainda (veja Saves)."));
        return;
    }
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Save");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 16);
    if (ImGui::BeginCombo("##save", g_file.c_str())) {
        for (const std::string& file : g_files) {
            std::string label = file == current_save() ? file + L("  (in use)", "  (em uso)") : file;
            if (ImGui::Selectable(label.c_str(), file == g_file)) {
                show(file);
                g_status.clear();
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    bool running = g_game_running;
    ImGui::BeginDisabled(running);
    if (ImGui::Button(L("Check this save", "Conferir este save"))) check_save();
    ImGui::EndDisabled();
    if (running && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", L("The game is running: it checks the save itself.", "O jogo está aberto: ele mesmo confere o save."));
    if (!g_status.empty()) {
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", g_status.c_str());
    }
    ImGui::Spacing();
    ImGui::BeginChild("list", ImVec2(0, 0));
    ntr::ui::achievements_list(g_achievements, renderer);
    ImGui::EndChild();
}

}  // namespace launcher
