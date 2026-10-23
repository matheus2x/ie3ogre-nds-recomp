// ie3-launcher: first-run preparation, settings, mods (to come) and updates.
// See launcher.h for the folder layout.
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <imgui.h>
#include <ntr/ui.h>

#include <cstdarg>
#include <cstdlib>
#include <cstdio>
#include <mutex>

#include "launcher.h"

namespace fs = std::filesystem;

namespace ntr {

// launcher.log next to the launcher, plus the terminal.
void log_printf(const char* fmt, ...) {
    static std::mutex mutex;
    static FILE* file = [] {
        const char* base = SDL_GetBasePath();
        return open_file(fs_path(std::string(base ? base : "") + "launcher.log"), "w");
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

void on_dump(const std::string& path) {
    std::lock_guard lock(g_drop_mutex);
    g_dropped = path;
}

void choose_dump(SDL_Window* window) {
    static const SDL_DialogFileFilter kFilters[] = {{"Dump de Nintendo DS (*.nds)", "nds"}, {"Todos os arquivos", "*"}};
    SDL_ShowOpenFileDialog(
        [](void*, const char* const* files, int) {
            if (files && files[0]) on_dump(files[0]);
        },
        nullptr, window, kFilters, 2, nullptr, false);
}

void play() {
    if (!start_process({paths().game()}, paths().root)) {
        NTR_LOG("[launcher] cannot start %s\n", paths().game().c_str());
        return;
    }
    NTR_LOG("[launcher] started the game\n");
    if (settings().get_bool("launcher", "close_on_play")) g_quit = true;
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
    if (ImGui::CollapsingHeader("Detalhes")) {
        ImGui::BeginChild("log", ImVec2(0, 160), ImGuiChildFlags_Borders);
        for (const std::string& line : g_job.log) ImGui::TextUnformatted(line.c_str());
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) ImGui::SetScrollHereY(1.0f);
        ImGui::EndChild();
    }
}

void play_tab(SDL_Window* window) {
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.5f);
    ImGui::TextUnformatted("Inazuma Eleven 3: Sekai heno Chousen!! The Ogre");
    ImGui::PopFont();
    ImGui::TextDisabled("Port nativo por recompilação estática. Versão %s.", LAUNCHER_VERSION);
    ImGui::Separator();
    ImGui::Spacing();

    if (g_job.running) {
        job_view();
        if (ImGui::Button("Cancelar")) g_job.cancel = true;
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
            ImGui::TextWrapped("Esta pasta não tem o sdk/. Baixe a release completa, ou gere o SDK com "
                               "\"python3 tools/build.py --sdk\" numa cópia do código-fonte.");
            break;
        case Readiness::NeedsDump:
            ImGui::TextWrapped("Para jogar, o launcher precisa do dump do SEU cartucho (.nds, código BOEJ, sem modificações). "
                               "Ele é usado só agora: o launcher confere o dump, cria o boej.pak e compila o jogo "
                               "neste computador. Nada do jogo vem com o launcher.");
            ImGui::Spacing();
            big_button_style(true);
            if (ImGui::Button("Escolher o dump (.nds)...", big)) choose_dump(window);
            big_button_style(false);
            ImGui::SameLine();
            ImGui::TextDisabled("ou arraste o arquivo\npara esta janela");
            ImGui::Spacing();
            ImGui::TextDisabled("Na primeira vez, o launcher também baixa o compilador (~190 MB) e leva alguns minutos.");
            break;
        case Readiness::NeedsBuild:
            ImGui::TextWrapped("O jogo precisa ser compilado para esta versão do launcher (leva alguns minutos; o dump não é necessário).");
            ImGui::Spacing();
            big_button_style(true);
            if (ImGui::Button("Preparar o jogo", big)) prepare_start(g_job, "");
            big_button_style(false);
            break;
        case Readiness::Ready:
            big_button_style(true);
            if (ImGui::Button("Jogar", big)) play();
            big_button_style(false);
            if (update_available()) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.5f, 0.85f, 0.5f, 1), "Há uma versão nova (aba Atualizações).");
            }
            ImGui::Spacing();
            if (ImGui::Button("Abrir a pasta do jogo")) open_folder(paths().root);
            ImGui::SameLine();
            if (ImGui::Button("Recompilar")) prepare_start(g_job, "");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Compila o jogo de novo (para testes; normalmente não é preciso)");
            break;
    }
}

void mods_tab() {
    ImGui::TextWrapped("Em breve: adicionar e remover mods (traduções, texturas, ajustes).");
    ImGui::Spacing();
    ImGui::TextWrapped("Os mods vão ficar na pasta mods/, ao lado do launcher. Por enquanto, o launcher só lista o que estiver lá.");
    ImGui::Spacing();
    std::error_code ec;
    int count = 0;
    if (ImGui::BeginChild("mods", ImVec2(0, 160), ImGuiChildFlags_Borders)) {
        for (const auto& entry : fs::directory_iterator(ntr::fs_path(paths().mods()), ec)) {
            ImGui::BulletText("%s", ntr::utf8_string(entry.path().filename()).c_str());
            count++;
        }
        if (!count) ImGui::TextDisabled("(nenhum mod)");
    }
    ImGui::EndChild();
    if (ImGui::Button("Abrir a pasta mods")) open_folder(paths().mods());
}

// ie3-launcher --prepare [dump.nds]: the same preparation without a window (for tests,
// or for players who prefer a terminal). Progress goes to the terminal and launcher.log.
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

}  // namespace

}  // namespace launcher

int main(int argc, char** argv) {
    using namespace launcher;
    if (argc >= 2 && std::string(argv[1]) == "--prepare") {
        NTR_LOG("[launcher] %s in %s (sdk %s)\n", LAUNCHER_VERSION, paths().root.c_str(), sdk_version().c_str());
        return prepare_cli(argc >= 3 ? argv[2] : "");
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
    settings_load();

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

    if (settings().get_bool("launcher", "check_updates")) update_check_start();

    // Tests: IE3_LAUNCHER_TAB opens that tab (0-3) and IE3_LAUNCHER_SHOT saves the window
    // as a BMP after a second, then quits.
    const char* shot = std::getenv("IE3_LAUNCHER_SHOT");
    const char* tab_env = std::getenv("IE3_LAUNCHER_TAB");
    int select_tab = tab_env ? std::atoi(tab_env) : -1;
    int frames = 0;

    while (!g_quit) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_EVENT_GAMEPAD_ADDED) SDL_OpenGamepad(ev.gdevice.which);
            if (settings_capturing() && (ev.type == SDL_EVENT_KEY_DOWN || ev.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN ||
                                         ev.type == SDL_EVENT_GAMEPAD_AXIS_MOTION)) {
                settings_event(ev);
                continue;  // the key is the binding, not navigation
            }
            ntr::ui::process_event(ev);
            if (ev.type == SDL_EVENT_QUIT) g_quit = true;
            if (ev.type == SDL_EVENT_DROP_FILE && ev.drop.data && !g_job.running) on_dump(ev.drop.data);
        }
        if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) {
            SDL_Delay(50);
            continue;
        }
        ntr::ui::begin_frame();

        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->WorkPos);
        ImGui::SetNextWindowSize(vp->WorkSize);
        ImGui::Begin("main", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
        if (ImGui::BeginTabBar("tabs")) {
            auto flags = [&](int tab) { return select_tab == tab ? ImGuiTabItemFlags_SetSelected : 0; };
            if (ImGui::BeginTabItem("Jogar", nullptr, flags(0))) {
                play_tab(window);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Configurações", nullptr, flags(1))) {
                ImGui::BeginChild("settings");
                settings_ui();
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Mods", nullptr, flags(2))) {
                mods_tab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(update_available() ? "Atualizações (nova!)###updates" : "Atualizações###updates",
                                    nullptr, flags(3))) {
                update_ui();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
            select_tab = -1;
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
