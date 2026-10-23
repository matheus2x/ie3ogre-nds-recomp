// Shared Dear ImGui screens: the settings panel (boej.ini edited with widgets and saved on
// every change; bindings captured by pressing the key or gamepad input) and the game's
// pause menu around it.
#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <vector>

#include <ntr/ui.h>

namespace ntr::ui {

namespace {

// The panel being drawn: set at the start of settings_panel.
Config* g_config = nullptr;
std::string g_path;
SettingsOptions g_options;
std::string g_status;

struct ActionInfo {
    const char* key;    // in boej.ini
    const char* label;  // for the player
};
const ActionInfo kButtons[] = {
    {"A", "A"}, {"B", "B"}, {"X", "X"}, {"Y", "Y"}, {"L", "L"}, {"R", "R"},
    {"START", "Start"}, {"SELECT", "Select"}, {"UP", "Cima"}, {"DOWN", "Baixo"},
    {"LEFT", "Esquerda"}, {"RIGHT", "Direita"},
};
const ActionInfo kShortcuts[] = {
    {"menu", "Menu de pausa"},
    {"fast_forward", "Acelerar (segurar)"},
    {"fast_forward_toggle", "Acelerar (liga/desliga)"},
    {"turbo", "A automático (segurar)"},
    {"turbo_toggle", "A automático (liga/desliga)"},
    {"screenshot", "Captura de tela"},
    {"debug_dump", "Registrar problema (F8)"},
    {"fullscreen", "Tela cheia"},
};

// A binding being captured. It keeps its own config pointer: the capture ends in an event,
// outside the panel's frame.
struct Capture {
    bool active = false;
    bool gamepad = false;
    bool append = false;  // add to the list instead of replacing it
    std::string key;
    std::string label;
};
Capture g_capture;

void save() {
    g_status = config_save(g_path, *g_config) ? "Salvo em boej.ini." : "Não foi possível gravar o boej.ini.";
    if (g_options.on_change) g_options.on_change();
}

void set(const char* section, const char* key, const std::string& value) {
    g_config->set(section, key, value);
    save();
}

std::vector<std::string> split(const std::string& list) {
    std::vector<std::string> items;
    std::stringstream in(list);
    std::string item;
    while (std::getline(in, item, ',')) {
        size_t a = item.find_first_not_of(" \t"), b = item.find_last_not_of(" \t");
        if (a != std::string::npos) items.push_back(item.substr(a, b - a + 1));
    }
    return items;
}

void bind_input(const std::string& input) {
    g_capture.active = false;
    if (!g_config) return;
    const char* section = g_capture.gamepad ? "gamepad" : "keyboard";
    std::string value = input;
    if (g_capture.append) {
        bool present = false;
        for (const std::string& i : split(g_config->get(section, g_capture.key))) {
            present |= SDL_strcasecmp(i.c_str(), input.c_str()) == 0;
        }
        value = g_config->get(section, g_capture.key);
        if (!present) value = value.empty() ? input : value + ", " + input;
    }
    set(section, g_capture.key.c_str(), value);
}

std::string pad_button_name(int button) {
    switch (button) {
        case SDL_GAMEPAD_BUTTON_SOUTH: return "south";
        case SDL_GAMEPAD_BUTTON_EAST: return "east";
        case SDL_GAMEPAD_BUTTON_WEST: return "west";
        case SDL_GAMEPAD_BUTTON_NORTH: return "north";
        default: {
            const char* n = SDL_GetGamepadStringForButton(SDL_GamepadButton(button));
            return n ? n : "";
        }
    }
}

// What the position names mean, for the tooltip: "east" is the right button of the
// diamond whatever its label.
std::string describe_pad(const std::string& value) {
    static const std::pair<const char*, const char*> kNames[] = {
        {"south", "botão de baixo (A no Xbox, B no Nintendo)"}, {"east", "botão da direita (B no Xbox, A no Nintendo)"},
        {"west", "botão da esquerda (X no Xbox, Y no Nintendo)"}, {"north", "botão de cima (Y no Xbox, X no Nintendo)"},
    };
    std::string out;
    for (const std::string& item : split(value)) {
        for (const auto& [name, desc] : kNames) {
            if (SDL_strcasecmp(item.c_str(), name) == 0) out += (out.empty() ? "" : "\n") + item + ": " + desc;
        }
    }
    return out;
}

void binding_cell(const ActionInfo& action, bool gamepad) {
    const char* section = gamepad ? "gamepad" : "keyboard";
    std::string value = g_config->get(section, action.key);
    ImGui::PushID(section);
    ImGui::PushID(action.key);
    if (ImGui::SmallButton("Mudar")) g_capture = {true, gamepad, false, action.key, action.label};
    ImGui::SameLine();
    if (ImGui::SmallButton("+")) g_capture = {true, gamepad, true, action.key, action.label};
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Adicionar outra tecla ou botão");
    ImGui::SameLine();
    if (ImGui::SmallButton("x")) set(section, action.key, "");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Desligar");
    ImGui::SameLine();
    ImGui::TextWrapped("%s", value.empty() ? "(nenhum)" : value.c_str());
    if (gamepad && ImGui::IsItemHovered()) {
        std::string about = describe_pad(value);
        if (!about.empty()) ImGui::SetTooltip("%s", about.c_str());
    }
    ImGui::PopID();
    ImGui::PopID();
}

void bindings_table(const char* id, const ActionInfo* actions, size_t count) {
    if (!ImGui::BeginTable(id, 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp)) return;
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 7.5f);
    ImGui::TableSetupColumn("Teclado");
    ImGui::TableSetupColumn("Controle");
    ImGui::TableHeadersRow();
    for (size_t i = 0; i < count; i++) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(actions[i].label);
        ImGui::TableNextColumn();
        binding_cell(actions[i], false);
        ImGui::TableNextColumn();
        binding_cell(actions[i], true);
    }
    ImGui::EndTable();
}

void capture_popup() {
    if (g_capture.active && !ImGui::IsPopupOpen("Capturar")) ImGui::OpenPopup("Capturar");
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Capturar", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
        ImGui::Text("%s: %s", g_capture.label.c_str(),
                    g_capture.gamepad ? "aperte um botão ou mova um eixo do controle" : "aperte uma tecla");
        ImGui::TextDisabled("Esc cancela");
        if (!g_capture.active) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void load_font(float scale) {
    ImGuiIO& io = ImGui::GetIO();
    static const char* const kFonts[] = {
        "C:/Windows/Fonts/segoeui.ttf",
        "/usr/share/fonts/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    };
    for (const char* path : kFonts) {
        std::error_code ec;
        if (std::filesystem::exists(path, ec) && io.Fonts->AddFontFromFileTTF(path, 18.0f * scale)) return;
    }
    ImFontConfig config;
    config.SizePixels = 13.0f * scale;
    io.Fonts->AddFontDefault(&config);
}

// A confirmation popup; true when confirmed.
bool confirm(const char* id, const char* text, const char* yes) {
    bool ok = false;
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal(id, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
        ImGui::TextUnformatted(text);
        ImGui::Spacing();
        if (ImGui::Button(yes)) {
            ok = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancelar")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    return ok;
}

}  // namespace

void init(SDL_Window* window, SDL_Renderer* renderer, float scale) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    ImGui::StyleColorsDark();
    ImGui::GetStyle().ScaleAllSizes(scale);
    ImGui::GetStyle().FrameRounding = 4 * scale;
    load_font(scale);
    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);
}

void shutdown() {
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
}

void process_event(const SDL_Event& ev) { ImGui_ImplSDL3_ProcessEvent(&ev); }

void begin_frame() {
    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
}

void end_frame(SDL_Renderer* renderer) {
    ImGui::Render();
    ImGuiIO& io = ImGui::GetIO();
    SDL_SetRenderScale(renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
    SDL_SetRenderScale(renderer, 1, 1);
}

bool capturing() { return g_capture.active; }

void capture_event(const SDL_Event& ev) {
    if (ev.type == SDL_EVENT_KEY_DOWN && !ev.key.repeat) {
        if (ev.key.key == SDLK_ESCAPE) {
            g_capture.active = false;
        } else if (!g_capture.gamepad) {
            bind_input(SDL_GetKeyName(ev.key.key));
        }
    } else if (g_capture.gamepad && ev.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
        std::string name = pad_button_name(ev.gbutton.button);
        if (!name.empty()) bind_input(name);
    } else if (g_capture.gamepad && ev.type == SDL_EVENT_GAMEPAD_AXIS_MOTION && std::abs(int(ev.gaxis.value)) > 20000) {
        const char* name = SDL_GetGamepadStringForAxis(SDL_GamepadAxis(ev.gaxis.axis));
        bool trigger = ev.gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || ev.gaxis.axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER;
        if (name) bind_input(std::string(name) + (trigger ? "" : ev.gaxis.value > 0 ? "+" : "-"));
    }
}

void settings_panel(Config& config, const std::string& path, const SettingsOptions& options) {
    g_config = &config;
    g_path = path;
    g_options = options;
    bool in_game = options.in_game;
    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.6f);  // room for the labels
    if (ImGui::CollapsingHeader("Vídeo", ImGuiTreeNodeFlags_DefaultOpen)) {
        bool wide = config.get_bool("video", "wide_layout");
        if (ImGui::Checkbox("Tela larga (16:9, tela de baixo ampliada)", &wide)) set("video", "wide_layout", wide ? "true" : "false");
        ImGui::BeginDisabled(wide);  // the wide window is sized from the monitor
        int scale = config.get_int("video", "window_scale", 1, 16);
        char format[48];
        std::snprintf(format, sizeof format, "%%d× (%d×%d)", 256 * scale, 384 * scale);
        if (ImGui::SliderInt("Tamanho da janela", &scale, 1, 6, format)) set("video", "window_scale", std::to_string(scale));
        ImGui::EndDisabled();

        int res = config.get_int("video", "resolution_3d", 1, 4) - 1;
        const char* kRes[] = {"1× (nativa, 256×192)", "2×", "3×", "4×"};
        if (ImGui::Combo("Resolução do 3D", &res, kRes, 4)) set("video", "resolution_3d", std::to_string(res + 1));

        int filter = SDL_strcasecmp(config.get("video", "filter").c_str(), "linear") == 0;
        const char* kFilters[] = {"Nítido (nearest)", "Suavizado (linear)"};
        if (ImGui::Combo("Ampliação", &filter, kFilters, 2)) set("video", "filter", filter ? "linear" : "nearest");

        bool fullscreen = config.get_bool("video", "fullscreen");
        if (ImGui::Checkbox(in_game ? "Tela cheia" : "Abrir em tela cheia", &fullscreen)) {
            set("video", "fullscreen", fullscreen ? "true" : "false");
        }
        ImGui::BeginDisabled(wide);
        bool integer = config.get_bool("video", "integer_scale");
        if (ImGui::Checkbox("Ampliar só em múltiplos inteiros", &integer)) set("video", "integer_scale", integer ? "true" : "false");
        ImGui::EndDisabled();
    }
    if (ImGui::CollapsingHeader("Áudio", ImGuiTreeNodeFlags_DefaultOpen)) {
        int volume = config.get_int("audio", "volume", 0, 100);
        if (ImGui::SliderInt("Volume", &volume, 0, 100, "%d%%")) set("audio", "volume", std::to_string(volume));
    }
    if (ImGui::CollapsingHeader("Controles", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextWrapped("\"Mudar\" troca pela próxima tecla ou botão que você apertar; \"+\" adiciona mais um; \"x\" desliga.");
        bindings_table("buttons", kButtons, std::size(kButtons));
        ImGui::Spacing();
        ImGui::TextUnformatted("Atalhos");
        bindings_table("shortcuts", kShortcuts, std::size(kShortcuts));
    }
    if (!in_game && ImGui::CollapsingHeader("Launcher")) {
        bool check = config.get_bool("launcher", "check_updates");
        if (ImGui::Checkbox("Procurar atualizações ao abrir", &check)) set("launcher", "check_updates", check ? "true" : "false");
        bool close = config.get_bool("launcher", "close_on_play");
        if (ImGui::Checkbox("Fechar o launcher quando o jogo abrir", &close)) set("launcher", "close_on_play", close ? "true" : "false");
    }
    ImGui::PopItemWidth();
    ImGui::Separator();
    if (ImGui::Button("Restaurar o padrão")) ImGui::OpenPopup("Restaurar");
    if (confirm("Restaurar", "Voltar todas as configurações e controles ao padrão?", "Restaurar")) {
        config_load("", config);
        save();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s", g_status.c_str());
    capture_popup();
}

PauseAction pause_menu(Config& config, const std::string& path, const SettingsOptions& options) {
    PauseAction action = PauseAction::None;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x * 0.92f, vp->WorkSize.y * 0.92f), ImGuiCond_Always);
    ImGui::Begin("Jogo pausado", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                                              ImGuiWindowFlags_NoSavedSettings);
    if (ImGui::Button("Continuar")) action = PauseAction::Resume;
    ImGui::SameLine();
    if (ImGui::Button("Reiniciar o jogo")) ImGui::OpenPopup("Reiniciar");
    ImGui::SameLine();
    if (ImGui::Button("Sair do jogo")) ImGui::OpenPopup("Sair");
    if (confirm("Reiniciar", "Voltar à tela de título? O progresso desde o último save será perdido.", "Reiniciar")) {
        action = PauseAction::Restart;
    }
    if (confirm("Sair", "Fechar o jogo? O progresso desde o último save será perdido.", "Sair")) action = PauseAction::Quit;
    ImGui::TextDisabled("As mudanças valem na hora e ficam salvas no boej.ini.");
    ImGui::Separator();
    ImGui::BeginChild("settings");
    settings_panel(config, path, options);
    ImGui::EndChild();
    ImGui::End();
    return action;
}

}  // namespace ntr::ui
