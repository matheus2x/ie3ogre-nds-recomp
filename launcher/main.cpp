// ie3-launcher: first-run preparation, settings, mods and updates.
// See launcher.h for the folder layout.
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <imgui.h>
#include <ntr/ui.h>

#include <cctype>
#include <cstdarg>
#include <cstdlib>
#include <cstdio>
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
int g_select_tab = -1;          // a tab to open on the next frame
Uint32 g_game_exited_event = 0;  // pushed by the thread that waits for the game

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

void big_button_style(bool on) {
    if (on) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16f, 0.48f, 0.86f, 1));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.24f, 0.56f, 0.94f, 1));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.12f, 0.40f, 0.76f, 1));
    } else {
        ImGui::PopStyleColor(3);
    }
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

void play_tab(SDL_Window* window) {
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.5f);
    ImGui::TextUnformatted("Inazuma Eleven 3: Sekai heno Chousen!! The Ogre");
    ImGui::PopFont();
    ImGui::TextDisabled(L("Native port through static recompilation. Version %s.",
                          "Port nativo por recompilação estática. Versão %s."), LAUNCHER_VERSION);
    language_picker();
    ImGui::Separator();
    ImGui::Spacing();

    if (g_job.running) {
        job_view();
        if (ImGui::Button(L("Cancel", "Cancelar"))) g_job.cancel = true;
        return;
    }
    {
        std::lock_guard lock(g_drop_mutex);
        if (!g_dropped.empty()) {
            prepare_start(g_job, g_dropped);
            g_dropped.clear();
            return;
        }
    }
    Readiness r = readiness();
    if (g_job.finished) {
        job_view();
        ImGui::Spacing();
    }
    const ImVec2 big(260, 54);
    switch (r) {
        case Readiness::NoSdk:
            ImGui::TextWrapped("%s", L("This folder has no sdk/. Download the full release, or make the SDK with "
                                       "\"python3 tools/build.py --sdk\" in a copy of the source code.",
                                       "Esta pasta não tem o sdk/. Baixe a release completa, ou gere o SDK com "
                                       "\"python3 tools/build.py --sdk\" numa cópia do código-fonte."));
            break;
        case Readiness::NeedsDump:
            ImGui::TextWrapped("%s", L("To play, the launcher needs a dump of YOUR cartridge (.nds, code BOEJ, unmodified). "
                                       "It is only used now: the launcher checks the dump, creates boej.pak and compiles the "
                                       "game on this computer. Nothing from the game comes with the launcher.",
                                       "Para jogar, o launcher precisa do dump do SEU cartucho (.nds, código BOEJ, sem modificações). "
                                       "Ele é usado só agora: o launcher confere o dump, cria o boej.pak e compila o jogo "
                                       "neste computador. Nada do jogo vem com o launcher."));
            ImGui::Spacing();
            big_button_style(true);
            if (ImGui::Button(L("Choose the dump (.nds)...", "Escolher o dump (.nds)..."), big)) choose_dump(window);
            big_button_style(false);
            ImGui::SameLine();
            ImGui::TextDisabled("%s", L("or drag the file\nonto this window", "ou arraste o arquivo\npara esta janela"));
            ImGui::Spacing();
            ImGui::TextDisabled("%s", L("The first time, the launcher also downloads the compiler (~190 MB) and takes a few minutes.",
                                        "Na primeira vez, o launcher também baixa o compilador (~190 MB) e leva alguns minutos."));
            break;
        case Readiness::NeedsBuild:
            ImGui::TextWrapped("%s", L("The game needs to be compiled for this version of the launcher (it takes a few minutes; "
                                       "the dump is not needed).",
                                       "O jogo precisa ser compilado para esta versão do launcher (leva alguns minutos; o dump "
                                       "não é necessário)."));
            ImGui::Spacing();
            big_button_style(true);
            if (ImGui::Button(L("Prepare the game", "Preparar o jogo"), big)) prepare_start(g_job, "");
            big_button_style(false);
            break;
        case Readiness::Ready:
            big_button_style(true);
            if (ImGui::Button(L("Play", "Jogar"), big)) play(window);
            big_button_style(false);
            ImGui::SameLine();
            ImGui::BeginGroup();
            ImGui::TextDisabled("Save: %s", current_save().c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton(L("Change", "Trocar"))) g_select_tab = 3;
            if (update_available()) {
                ImGui::TextColored(ImVec4(0.5f, 0.85f, 0.5f, 1), "%s",
                                   L("A new version is out (Updates tab).", "Há uma versão nova (aba Atualizações)."));
            }
            ImGui::EndGroup();
            ImGui::Spacing();
            if (ImGui::Button(L("Open the game folder", "Abrir a pasta do jogo"))) open_folder(paths().root);
            ImGui::SameLine();
            if (ImGui::Button(L("Open the logs folder", "Abrir a pasta de logs"))) open_folder(paths().logs());
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", L("A log and a recording of each game session, and historico.txt with all of them",
                                          "Um registro e uma gravação por sessão de jogo, e o historico.txt com todas elas"));
            }
            ImGui::SameLine();
            if (ImGui::Button(L("Recompile", "Recompilar"))) prepare_start(g_job, "");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", L("Compiles the game again (for testing; normally not needed)",
                                          "Compila o jogo de novo (para testes; normalmente não é preciso)"));
            }
            break;
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
    if (!SDL_CreateWindowAndRenderer("Inazuma Eleven 3 - The Ogre (launcher)", int(820 * scale), int(640 * scale),
                                     SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY, &window, &renderer)) {
        NTR_LOG("[launcher] cannot create the window: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetRenderVSync(renderer, 1);

    ntr::ui::init(window, renderer, scale);
    add_japanese_font(scale);
    g_game_exited_event = SDL_RegisterEvents(1);

    if (settings().get_bool("launcher", "check_updates")) update_check_start();

    // Tests: IE3_LAUNCHER_TAB opens that tab (0-5, 5 = Achievements), IE3_LAUNCHER_MOD selects that mod folder
    // in the Mods tab and IE3_LAUNCHER_SHOT saves the window as a BMP after a second, then quits.
    const char* shot = std::getenv("IE3_LAUNCHER_SHOT");
    const char* tab_env = std::getenv("IE3_LAUNCHER_TAB");
    g_select_tab = tab_env ? std::atoi(tab_env) : -1;
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
                    g_select_tab = 2;
                } else if (ext == ".sav") {
                    saves_drop(path);
                    g_select_tab = 3;
                } else if (!g_job.running) {
                    on_dump(path);
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
        if (ImGui::BeginTabBar("tabs")) {
            int select_tab = g_select_tab;  // a tab can ask for another one (it opens on the next frame)
            g_select_tab = -1;
            auto flags = [&](int tab) { return select_tab == tab ? ImGuiTabItemFlags_SetSelected : 0; };
            if (ImGui::BeginTabItem(L("Play###play", "Jogar###play"), nullptr, flags(0))) {
                play_tab(window);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(L("Settings###settings", "Configurações###settings"), nullptr, flags(1))) {
                settings_ui();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Mods", nullptr, flags(2))) {
                mods_tab(window);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Saves", nullptr, flags(3))) {
                saves_tab(renderer);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(L("Achievements###achievements", "Conquistas###achievements"), nullptr, flags(5))) {
                achievements_tab(renderer);
                ImGui::EndTabItem();
            }
            const char* updates = update_available() ? L("Updates (new!)###updates", "Atualizações (nova!)###updates")
                                                     : L("Updates###updates", "Atualizações###updates");
            if (ImGui::BeginTabItem(updates, nullptr, flags(4))) {
                update_ui();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        ImGui::End();

        SDL_SetRenderDrawColor(renderer, 20, 22, 28, 255);
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
