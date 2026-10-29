// ie3-launcher: first-run preparation, settings, mods and updates.
// See launcher.h for the folder layout.
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <imgui.h>
#include <ntr/ui.h>

#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <mutex>

#include "launcher.h"

namespace fs = std::filesystem;

namespace ntr {

// logs/launcher.log (the previous run's kept as launcher.old.log), plus the terminal.
void log_printf(const char* fmt, ...) {
    static std::mutex mutex;
    static FILE* file = [] {
        const char* base = SDL_GetBasePath();
        fs::path dir = logs_dir(base ? base : "");
        std::error_code ec;
        fs::remove(dir / "launcher.old.log", ec);
        fs::rename(dir / "launcher.log", dir / "launcher.old.log", ec);
        return open_file(dir / "launcher.log", "w");
    }();
    char buf[4096];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    std::lock_guard lock(mutex);
    std::fputs(buf, stderr);
    if (file) {
        std::fputs(buf, file);
        std::fflush(file);
    }
}

}  // namespace ntr

namespace launcher {

const Paths& paths() {
    static const Paths p = [] {
        const char* base = SDL_GetBasePath();
        return Paths{base ? base : "./"};
    }();
    return p;
}

std::string format_bytes(uint64_t bytes) {
    char buf[32];
    if (bytes >= (1ull << 20)) std::snprintf(buf, sizeof buf, "%.0f MB", bytes / 1048576.0);
    else std::snprintf(buf, sizeof buf, "%.0f KB", bytes / 1024.0);
    return buf;
}

// "1.2.10" > "1.2.9"; a "-dev" or other suffix sorts before the plain version.
int compare_versions(const std::string& a, const std::string& b) {
    unsigned va[3] = {}, vb[3] = {};
    std::sscanf(a.c_str(), "%u.%u.%u", &va[0], &va[1], &va[2]);
    std::sscanf(b.c_str(), "%u.%u.%u", &vb[0], &vb[1], &vb[2]);
    for (int i = 0; i < 3; i++) {
        if (va[i] != vb[i]) return va[i] < vb[i] ? -1 : 1;
    }
    bool sa = a.find('-') != std::string::npos, sb = b.find('-') != std::string::npos;
    return sa == sb ? 0 : sa ? -1 : 1;
}

void open_folder(const std::string& path) {
    std::error_code ec;
    fs::create_directories(ntr::fs_path(path), ec);
    std::string url = "file:///" + path;
    for (char& c : url) {
        if (c == '\\') c = '/';
    }
    SDL_OpenURL(url.c_str());
}

namespace {

Job g_job;
std::mutex g_drop_mutex;
std::string g_dropped;  // a dump from the file dialog or dropped on the window
bool g_quit = false;
bool g_hidden = false;          // the window waits hidden while the game runs
Uint32 g_game_exited_event = 0;  // pushed by the thread that waits for the game

// The home menu and the pages it opens (the numbers of IE3_LAUNCHER_TAB).
enum class Screen { Home, Settings, Mods, Saves, Updates, Achievements };
Screen g_screen = Screen::Home;
double g_home_since = 0;  // when the home screen appeared, for the background's slide

void on_dump(const std::string& path) {
    std::lock_guard lock(g_drop_mutex);
    g_dropped = path;
}

void choose_dump(SDL_Window* window) {
    static SDL_DialogFileFilter filters[2];  // static: the dialog may read it after this returns
    filters[0] = {L("Nintendo DS dump (*.nds)", "Dump de Nintendo DS (*.nds)"), "nds"};
    filters[1] = {L("All files", "Todos os arquivos"), "*"};
    SDL_ShowOpenFileDialog(
        [](void*, const char* const* files, int) {
            if (files && files[0]) on_dump(files[0]);
        },
        nullptr, window, filters, 2, nullptr, false);
}

void play(SDL_Window* window) {
    auto on_exit = [] {
        SDL_Event ev = {};
        ev.type = g_game_exited_event;
        SDL_PushEvent(&ev);
    };
    if (!start_process({paths().game()}, paths().root, on_exit)) {
        NTR_LOG("[launcher] cannot start %s\n", paths().game().c_str());
        return;
    }
    NTR_LOG("[launcher] started the game\n");
    if (!settings().get_bool("launcher", "close_on_play")) return;
    if (settings().get_bool("launcher", "stay_closed")) {
        g_quit = true;
    } else {
        SDL_HideWindow(window);
        g_hidden = true;
    }
}

// The game closed: its pause menu may have changed boej.ini, and a hidden launcher comes back.
void game_exited(SDL_Window* window) {
    NTR_LOG("[launcher] the game closed\n");
    settings_load();
    if (!g_hidden) return;
    g_hidden = false;
    SDL_ShowWindow(window);
    SDL_RaiseWindow(window);
}

void job_view() {
    std::lock_guard lock(g_job.mutex);
    ImGui::TextUnformatted(g_job.step.c_str());
    if (g_job.running) {
        if (g_job.progress >= 0) {
            ImGui::ProgressBar(float(g_job.progress), ImVec2(-1, 0));
        } else {
            ImGui::ProgressBar(-float(ImGui::GetTime()), ImVec2(-1, 0), "");  // indeterminate
        }
    }
    if (g_job.finished && !g_job.ok) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0.45f, 0.4f, 1));
        ImGui::TextWrapped("%s", g_job.error.c_str());
        ImGui::PopStyleColor();
    }
    if (ImGui::CollapsingHeader(L("Details###details", "Detalhes###details"))) {
        ImGui::BeginChild("log", ImVec2(0, 160), ImGuiChildFlags_Borders);
        for (const std::string& line : g_job.log) ImGui::TextUnformatted(line.c_str());
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) ImGui::SetScrollHereY(1.0f);
        ImGui::EndChild();
    }
}

// The language, at the right of the subtitle (also under Settings > Launcher): the launcher
// starts in English, and a player who does not read it finds the choice at once.
void language_picker() {
    const char* label = "Language / Idioma";
    const char* kLanguages[] = {"English", "Português (Brasil)"};
    float combo = ImGui::CalcTextSize(kLanguages[1]).x + ImGui::GetFrameHeight() + ImGui::GetStyle().FramePadding.x * 2;
    float width = ImGui::CalcTextSize(label).x + ImGui::GetStyle().ItemInnerSpacing.x + combo;
    ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - width));
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", label);
    ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x);
    ImGui::SetNextItemWidth(combo);
    int lang = ntr::language() == ntr::Language::Portuguese;
    if (ImGui::Combo("##language", &lang, kLanguages, 2)) {
        ntr::set_language(lang ? ntr::Language::Portuguese : ntr::Language::English);
        settings_set("ui", "language", lang ? "pt" : "en");
    }
}

ImVec4 color(uint32_t c) { return ImGui::ColorConvertU32ToFloat4(c); }

ImU32 with_alpha(uint32_t c, float alpha) {
    return (c & 0x00FFFFFF) | (ImU32(std::clamp(alpha, 0.0f, 1.0f) * 255) << 24);
}

void push_bold(float factor) { ImGui::PushFont(ntr::ui::bold_font(), ImGui::GetStyle().FontSizeBase * factor); }

// Behind the home screen, faint: a lightning bolt and diagonal bands in the card's blue and
// the lightning's yellow. They slide in from the right when the screen appears.
void draw_background() {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetWindowPos(), size = ImGui::GetWindowSize();
    float t = std::clamp(float(ImGui::GetTime() - g_home_since) / 2.5f, 0.0f, 1.0f);
    float ease = 1 - (1 - t) * (1 - t) * (1 - t);
    float shift = (1 - ease) * size.x * 0.12f, fade = std::min(1.0f, t * 2.5f);
    auto at = [&](float x, float y) { return ImVec2(p.x + x * size.x + shift, p.y + y * size.y); };
    auto band = [&](float x, float width, uint32_t c, float alpha) {  // from the top to the bottom, leaning
        ImVec2 q[4] = {at(x, -0.05f), at(x + width, -0.05f), at(x + width - 0.32f, 1.05f), at(x - 0.32f, 1.05f)};
        dl->AddConvexPolyFilled(q, 4, with_alpha(c, alpha * fade));
    };
    band(0.86f, 0.22f, ntr::ui::kPrimary, 0.16f);
    band(1.12f, 0.30f, ntr::ui::kPrimaryActive, 0.30f);
    band(0.80f, 0.010f, ntr::ui::kHighlight, 0.30f);
    band(1.09f, 0.006f, ntr::ui::kMagenta, 0.35f);
    // The bolt, in a box from (x0, y0) to (x1, y1) of the window.
    const float x0 = 0.58f, x1 = 0.92f, y0 = 0.08f, y1 = 0.96f;
    auto in_box = [&](float x, float y) { return at(x0 + x * (x1 - x0), y0 + y * (y1 - y0)); };
    const ImVec2 bolt[] = {in_box(0.48f, 0), in_box(0.92f, 0), in_box(0.62f, 0.38f), in_box(0.90f, 0.38f),
                           in_box(0.12f, 1), in_box(0.38f, 0.52f), in_box(0.10f, 0.52f)};
    dl->AddConcavePolyFilled(bolt, 7, with_alpha(ntr::ui::kHighlight, 0.10f * fade));
    dl->AddPolyline(bolt, 7, with_alpha(ntr::ui::kHighlight, 0.35f * fade), ImDrawFlags_Closed, 2.0f);
}

constexpr float kMenuSize = 1.45f;  // the menu's text, times the normal size

// An item of the home menu: the label large and bold; hovered or focused, a yellow marker and a
// blue glow fading to the right. `note` is a short text after the label, in `note_color`.
bool menu_item(const char* label, const char* note = nullptr, uint32_t note_color = 0, bool enabled = true) {
    push_bold(kMenuSize);
    const float em = ImGui::GetFontSize(), pad = em * 0.3f, marker = em * 0.8f;
    const char* end = std::strstr(label, "##");  // the id after "###" is not shown
    const ImVec2 text = ImGui::CalcTextSize(label, end);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const ImVec2 size(std::max(em * 12, ImGui::GetContentRegionAvail().x * 0.5f), text.y + pad * 2);
    ImGui::PushStyleColor(ImGuiCol_NavCursor, IM_COL32(0, 0, 0, 0));  // the glow shows the focus instead
    ImGui::BeginDisabled(!enabled);
    bool pressed = ImGui::InvisibleButton(label, size, ImGuiButtonFlags_EnableNav);
    bool lit = enabled && (ImGui::IsItemHovered() || (ImGui::IsItemFocused() && ImGui::GetIO().NavVisible));
    ImGui::EndDisabled();
    ImGui::PopStyleColor();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 q(p.x + size.x, p.y + size.y);
    if (lit) {
        ImU32 from = with_alpha(ntr::ui::kPrimary, 0.55f), to = with_alpha(ntr::ui::kPrimary, 0);
        dl->AddRectFilledMultiColor(p, q, from, to, to, from);
        dl->AddRectFilled(p, ImVec2(p.x + 3, q.y), ntr::ui::kHighlight);
        float cy = p.y + size.y / 2, x = p.x + pad + em * 0.1f, h = em * 0.26f;
        dl->AddTriangleFilled(ImVec2(x, cy - h), ImVec2(x + h * 1.3f, cy), ImVec2(x, cy + h), ntr::ui::kHighlight);
    }
    ImU32 ink = !enabled ? ImGui::GetColorU32(ImGuiCol_TextDisabled) : lit ? IM_COL32_WHITE : ImGui::GetColorU32(ImGuiCol_Text, 0.82f);
    float x = p.x + pad + marker + (lit ? em * 0.15f : 0);
    dl->AddText(ImVec2(x, p.y + pad), ink, label, end);
    ImGui::PopFont();
    if (note) {
        ImVec2 n = ImGui::CalcTextSize(note);
        dl->AddText(ImVec2(x + text.x + em * 0.6f, p.y + (size.y - n.y) / 2), note_color ? note_color : ImGui::GetColorU32(ImGuiCol_TextDisabled), note);
    }
    return pressed;
}

// Under the title: what the game still needs, the preparation's progress or its result.
void home_status(Readiness r) {
    if (g_job.running || g_job.finished) {
        job_view();
        if (g_job.running && ImGui::Button(L("Cancel", "Cancelar"))) g_job.cancel = true;
        return;
    }
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    switch (r) {
        case Readiness::NoSdk:
            ImGui::TextWrapped("%s", L("This folder has no sdk/. Download the full release, or make the SDK with "
                                       "\"python3 tools/build.py --sdk\" in a copy of the source code.",
                                       "Esta pasta não tem o sdk/. Baixe a release completa, ou gere o SDK com "
                                       "\"python3 tools/build.py --sdk\" numa cópia do código-fonte."));
            break;
        case Readiness::NeedsDump:
            ImGui::TextWrapped("%s", L("To play, the launcher needs a dump of YOUR cartridge (.nds, code BOEJ, unmodified). It "
                                       "checks the dump and compiles the game on this computer: nothing from the game comes "
                                       "with the launcher. The first time, it also downloads the compiler (~190 MB) and takes "
                                       "a few minutes.",
                                       "Para jogar, o launcher precisa do dump do SEU cartucho (.nds, código BOEJ, sem "
                                       "modificações). Ele confere o dump e compila o jogo neste computador: nada do jogo vem "
                                       "com o launcher. Na primeira vez, também baixa o compilador (~190 MB) e leva alguns "
                                       "minutos."));
            break;
        case Readiness::NeedsBuild:
            ImGui::TextWrapped("%s", L("The game needs to be compiled for this version of the launcher (it takes a few minutes; "
                                       "the dump is not needed).",
                                       "O jogo precisa ser compilado para esta versão do launcher (leva alguns minutos; o dump "
                                       "não é necessário)."));
            break;
        case Readiness::Ready:
            break;
    }
    ImGui::PopStyleColor();
}

// A small text button, for the links at the bottom of the home screen.
bool link(const char* label, const char* tooltip = nullptr) {
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x * 0.6f, ImGui::GetStyle().FramePadding.y));
    bool pressed = ImGui::Button(label);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
    if (tooltip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tooltip);
    return pressed;
}

// The home screen, minimal as in the N64 Recomp launchers: the title at the top, the menu at the
// bottom left, the version, folders and language in the footer.
void home_screen(SDL_Window* window) {
    {
        std::lock_guard lock(g_drop_mutex);
        if (!g_dropped.empty() && !g_job.running) {
            prepare_start(g_job, g_dropped);
            g_dropped.clear();
        }
    }
    draw_background();
    const ImGuiStyle& style = ImGui::GetStyle();
    const float em = ImGui::GetFontSize(), margin = em * 1.6f;
    const ImVec2 origin = ImGui::GetCursorPos();
    ImGui::SetCursorPos(ImVec2(origin.x + margin, origin.y + margin * 0.6f));
    ImGui::BeginGroup();
    push_bold(0.85f);
    ImGui::PushStyleColor(ImGuiCol_Text, color(ntr::ui::kHighlight));
    ImGui::TextUnformatted("R E C O M P I L E D");
    ImGui::PopStyleColor();
    ImGui::PopFont();
    push_bold(2.6f);
    ImGui::TextUnformatted("Inazuma Eleven");
    ImGui::SameLine();
    ImGui::TextColored(color(ntr::ui::kMagenta), "3");  // pink, as on the logo
    ImGui::PopFont();
    ImGui::PushFont(nullptr, style.FontSizeBase * 1.25f);
    ImGui::TextDisabled("Sekai heno Chousen!! The Ogre");
    ImGui::PopFont();
    ImGui::EndGroup();

    // The footer's height and the menu's, from the bottom of the window up.
    const Readiness r = readiness();
    const bool busy = g_job.running;
    const int items = busy ? 0 : 7;
    const float item_h = style.FontSizeBase * style.FontScaleMain * kMenuSize * 1.6f;
    const float footer_h = ImGui::GetFrameHeight() + style.WindowPadding.y;
    const float bottom = ImGui::GetWindowHeight() - footer_h - style.WindowPadding.y;
    const float menu_y = bottom - items * item_h - em;

    // What the game needs, or the preparation, between the title and the menu.
    const float status_y = ImGui::GetCursorPosY() + em;
    const float status_w = std::min(ImGui::GetContentRegionAvail().x - margin, em * 34);
    if (menu_y - status_y > em * 2) {
        ImGui::SetCursorPos(ImVec2(origin.x + margin, status_y));
        ImGui::BeginChild("status", ImVec2(status_w, menu_y - status_y - em * 0.5f));
        home_status(r);
        ImGui::EndChild();
    }

    if (!busy) {
        // The labels in line with the title, the marker in the margin.
        ImGui::SetCursorPos(ImVec2(origin.x + margin - em * kMenuSize * 1.1f, menu_y));
        ImGui::BeginGroup();
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(style.ItemSpacing.x, 0));
        std::string save = "save: " + current_save();
        switch (r) {
            case Readiness::Ready:
                if (menu_item(L("Play###main", "Jogar###main"), save.c_str())) play(window);
                break;
            case Readiness::NeedsDump:
                if (menu_item(L("Choose the dump###main", "Escolher o dump###main"),
                              L("(.nds) or drag it here", "(.nds) ou arraste para cá"))) {
                    choose_dump(window);
                }
                break;
            case Readiness::NeedsBuild:
                if (menu_item(L("Prepare the game###main", "Preparar o jogo###main"))) prepare_start(g_job, "");
                break;
            case Readiness::NoSdk:
                menu_item(L("Play###main", "Jogar###main"), nullptr, 0, false);
                break;
        }
        if (ImGui::IsWindowAppearing()) ImGui::SetItemDefaultFocus();
        if (menu_item("Saves")) g_screen = Screen::Saves;
        if (menu_item(L("Achievements", "Conquistas"))) g_screen = Screen::Achievements;
        if (menu_item("Mods")) g_screen = Screen::Mods;
        if (menu_item(L("Settings", "Configurações"))) g_screen = Screen::Settings;
        if (menu_item(L("Updates", "Atualizações"), update_available() ? L("new version!", "versão nova!") : nullptr, ntr::ui::kMagenta)) {
            g_screen = Screen::Updates;
        }
        if (menu_item(L("Quit", "Sair"))) g_quit = true;
        ImGui::PopStyleVar();
        ImGui::EndGroup();
    }

    // The footer: the version and the folders on the left, the language on the right.
    ImGui::SetCursorPos(ImVec2(origin.x + margin - style.FramePadding.x * 0.6f, ImGui::GetWindowHeight() - footer_h));
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("v%s", LAUNCHER_VERSION);
    ImGui::SameLine(0, em);
    if (link(L("Game folder", "Pasta do jogo"))) open_folder(paths().root);
    ImGui::SameLine(0, 0);
    if (link("Logs", L("A log and a recording of each game session, and historico.txt with all of them",
                       "Um registro e uma gravação por sessão de jogo, e o historico.txt com todas elas"))) {
        open_folder(paths().logs());
    }
    if (r == Readiness::Ready && !busy) {
        ImGui::SameLine(0, 0);
        if (link(L("Recompile", "Recompilar"), L("Compiles the game again (for testing; normally not needed)",
                                                 "Compila o jogo de novo (para testes; normalmente não é preciso)"))) {
            prepare_start(g_job, "");
        }
    }
    language_picker();
}

// A page opened from the menu: "Back" (also Escape or the gamepad's B) and its title.
void page_header(const char* title) {
    bool back = ImGui::Button(L("< Back", "< Voltar"));
    bool popup = ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId);
    if (!popup && !ImGui::GetIO().WantTextInput &&
        (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false))) {
        back = true;
    }
    ImGui::SameLine(0, ImGui::GetFontSize());
    push_bold(1.6f);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(title);
    ImGui::PopFont();
    // Under it, a blue rule that starts in yellow.
    const float em = ImGui::GetFontSize();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + 2), with_alpha(ntr::ui::kPrimary, 0.55f));
    dl->AddRectFilled(p, ImVec2(p.x + em * 3, p.y + 2), ntr::ui::kHighlight);
    ImGui::Dummy(ImVec2(w, 2));
    ImGui::Spacing();
    if (back) {
        g_screen = Screen::Home;
        g_home_since = ImGui::GetTime();
    }
}

// ie3-launcher --prepare [dump.nds]: the same preparation without a window (for tests,
// or for players who prefer a terminal). Progress goes to the terminal and logs/launcher.log.
int prepare_cli(const std::string& dump) {
    Readiness r = readiness();
    if (r == Readiness::NoSdk) {
        NTR_LOG("error: no sdk/ next to the launcher\n");
        return 1;
    }
    if (r == Readiness::NeedsDump && dump.empty()) {
        NTR_LOG("error: the dump is needed: ie3-launcher --prepare <dump.nds>\n");
        return 1;
    }
    prepare_start(g_job, dump);
    int last_percent = -1;
    while (!g_job.finished) {
        SDL_Delay(500);
        std::lock_guard lock(g_job.mutex);
        int percent = g_job.progress >= 0 ? int(g_job.progress * 100) : -1;
        if (percent / 10 != last_percent / 10 && percent >= 0) NTR_LOG("[prepare] %d%%\n", percent);
        last_percent = percent;
    }
    if (!g_job.ok) NTR_LOG("error: %s\n", g_job.error.c_str());
    return g_job.ok ? 0 : 1;
}

// Names in the Saves tab can be in Japanese: a system font with kana and kanji, merged into
// the main one (glyphs are loaded only when shown).
void add_japanese_font(float scale) {
    static const char* const kFonts[] = {
        "C:/Windows/Fonts/YuGothM.ttc",
        "C:/Windows/Fonts/msgothic.ttc",
        "C:/Windows/Fonts/meiryo.ttc",
        "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
    };
    for (const char* path : kFonts) {
        std::error_code ec;
        if (!fs::exists(path, ec)) continue;
        ImFontConfig config;
        config.MergeMode = true;
        if (ImGui::GetIO().Fonts->AddFontFromFileTTF(path, 18.0f * scale, &config)) return;
    }
}

}  // namespace

}  // namespace launcher

int main(int argc, char** argv) {
    using namespace launcher;
    settings_load();  // the language, for the messages of the command-line modes too
    if (argc >= 2 && std::string(argv[1]) == "--prepare") {
        NTR_LOG("[launcher] %s in %s (sdk %s)\n", LAUNCHER_VERSION, paths().root.c_str(), sdk_version().c_str());
        return prepare_cli(argc >= 3 ? argv[2] : "");
    }
    if (argc == 3 && (std::string(argv[1]) == "--install-mod" || std::string(argv[1]) == "--mod-from-dump")) {
        return mods_cli(argv[1], argv[2]);
    }
    if (argc >= 2 && std::string(argv[1]) == "--update") {
        update_cleanup();
        return update_cli();
    }
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    NTR_LOG("[launcher] %s in %s (sdk %s)\n", LAUNCHER_VERSION, paths().root.c_str(), sdk_version().c_str());
    update_cleanup();

    float scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
    if (scale <= 0) scale = 1;
    SDL_Window* window;
    SDL_Renderer* renderer;
    if (!SDL_CreateWindowAndRenderer("Inazuma Eleven 3 - The Ogre (launcher)", int(1040 * scale), int(640 * scale),
                                     SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY, &window, &renderer)) {
        NTR_LOG("[launcher] cannot create the window: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetRenderVSync(renderer, 1);

    ntr::ui::init(window, renderer, scale);
    add_japanese_font(scale);
    g_game_exited_event = SDL_RegisterEvents(1);

    if (settings().get_bool("launcher", "check_updates")) update_check_start();

    // Tests: IE3_LAUNCHER_TAB opens that page (0-5 as in Screen, 5 = Achievements), IE3_LAUNCHER_MOD selects that
    // mod folder in Mods and IE3_LAUNCHER_SHOT saves the window as a BMP after a second, then quits (with the
    // home screen's background already in place).
    const char* shot = std::getenv("IE3_LAUNCHER_SHOT");
    const char* tab_env = std::getenv("IE3_LAUNCHER_TAB");
    if (tab_env) g_screen = Screen(std::clamp(std::atoi(tab_env), 0, 5));
    if (shot) g_home_since = -10;
    int frames = 0;

    while (!g_quit) {
        SDL_Event ev;
        if (g_hidden) SDL_WaitEvent(nullptr);  // nothing to draw until the game closes
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_EVENT_GAMEPAD_ADDED) SDL_OpenGamepad(ev.gdevice.which);
            if (settings_capturing() && (ev.type == SDL_EVENT_KEY_DOWN || ev.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN ||
                                         ev.type == SDL_EVENT_GAMEPAD_AXIS_MOTION)) {
                settings_event(ev);
                continue;  // the key is the binding, not navigation
            }
            ntr::ui::process_event(ev);
            if (ev.type == SDL_EVENT_QUIT) g_quit = true;
            if (ev.type == SDL_EVENT_DROP_FILE && ev.drop.data) {
                std::string path = ev.drop.data;
                std::string ext = ntr::utf8_string(ntr::fs_path(path).extension());
                for (char& c : ext) c = char(std::tolower(uint8_t(c)));
                std::error_code ec;
                if (ext == ".zip" || fs::is_directory(ntr::fs_path(path), ec)) {
                    mods_drop(path);
                    g_screen = Screen::Mods;
                } else if (ext == ".sav") {
                    saves_drop(path);
                    g_screen = Screen::Saves;
                } else if (!g_job.running) {
                    on_dump(path);
                    g_screen = Screen::Home;
                }
            }
            if (ev.type == g_game_exited_event) game_exited(window);
        }
        if (g_hidden) continue;
        if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) {
            SDL_Delay(50);
            continue;
        }
        ntr::ui::begin_frame();
        saves_update();

        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->WorkPos);
        ImGui::SetNextWindowSize(vp->WorkSize);
        ImGui::Begin("main", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
        switch (g_screen) {
            case Screen::Home:
                home_screen(window);
                break;
            case Screen::Settings:
                page_header(L("Settings", "Configurações"));
                settings_ui();
                break;
            case Screen::Mods:
                page_header("Mods");
                mods_tab(window);
                break;
            case Screen::Saves:
                page_header("Saves");
                saves_tab(renderer);
                break;
            case Screen::Achievements:
                page_header(L("Achievements", "Conquistas"));
                achievements_tab(renderer);
                break;
            case Screen::Updates:
                page_header(L("Updates", "Atualizações"));
                update_ui();
                break;
        }
        ImGui::End();

        SDL_SetRenderDrawColor(renderer, ntr::ui::kBackground & 0xFF, (ntr::ui::kBackground >> 8) & 0xFF,
                               (ntr::ui::kBackground >> 16) & 0xFF, 255);
        SDL_RenderClear(renderer);
        ntr::ui::end_frame(renderer);
        if (shot && ++frames == 60) {
            if (SDL_Surface* s = SDL_RenderReadPixels(renderer, nullptr)) {
                SDL_SaveBMP(s, shot);
                SDL_DestroySurface(s);
            }
            g_quit = true;
        }
        SDL_RenderPresent(renderer);
    }
    if (g_job.running) {
        g_job.cancel = true;
        while (g_job.running) SDL_Delay(50);
    }
    ntr::ui::shutdown();
    SDL_Quit();
    return 0;
}
