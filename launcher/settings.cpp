// The settings tab: boej.ini edited with widgets and saved on every change. Bindings are
// captured by pressing the key or gamepad input.
#include <SDL3/SDL.h>
#include <imgui.h>

#include <cstdlib>
#include <sstream>

#include "launcher.h"

namespace launcher {

namespace {

ntr::Config g_config;
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
    {"fast_forward", "Acelerar (segurar)"},
    {"fast_forward_toggle", "Acelerar (liga/desliga)"},
    {"turbo", "A automático (segurar)"},
    {"turbo_toggle", "A automático (liga/desliga)"},
    {"screenshot", "Captura de tela"},
    {"debug_dump", "Registrar problema (F8)"},
    {"fullscreen", "Tela cheia"},
};

// A binding being captured.
struct Capture {
    bool active = false;
    bool gamepad = false;
    bool append = false;  // add to the list instead of replacing it
    std::string key;
    std::string label;
};
Capture g_capture;

void save() {
    g_status = ntr::config_save(paths().config(), g_config) ? "Salvo em boej.ini." : "Não foi possível gravar o boej.ini.";
}

void set(const char* section, const char* key, const std::string& value) {
    g_config.set(section, key, value);
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
    const char* section = g_capture.gamepad ? "gamepad" : "keyboard";
    std::string value = input;
    if (g_capture.append) {
        std::vector<std::string> items = split(g_config.get(section, g_capture.key));
        bool present = false;
        for (const std::string& i : items) present |= SDL_strcasecmp(i.c_str(), input.c_str()) == 0;
        value = g_config.get(section, g_capture.key);
        if (!present) value = value.empty() ? input : value + ", " + input;
    }
    set(section, g_capture.key.c_str(), value);
    g_capture.active = false;
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

// Readable names next to the config's: "east" means the right button of the diamond.
std::string describe_pad(const std::string& value) {
    static const std::pair<const char*, const char*> kNames[] = {
        {"south", "baixo (A Xbox / B Nintendo)"}, {"east", "direita (B Xbox / A Nintendo)"},
        {"west", "esquerda (X Xbox / Y Nintendo)"}, {"north", "cima (Y Xbox / X Nintendo)"},
    };
    std::string out;
    for (const std::string& item : split(value)) {
        std::string text = item;
        for (const auto& [name, desc] : kNames) {
            if (SDL_strcasecmp(item.c_str(), name) == 0) text = item + " = " + desc;
        }
        out += (out.empty() ? "" : ", ") + text;
    }
    return out;
}

void binding_cell(const ActionInfo& action, bool gamepad) {
    const char* section = gamepad ? "gamepad" : "keyboard";
    std::string value = g_config.get(section, action.key);
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
    std::string shown = gamepad ? describe_pad(value) : value;
    ImGui::TextWrapped("%s", shown.empty() ? "(nenhum)" : shown.c_str());
    ImGui::PopID();
    ImGui::PopID();
}

void bindings_table(const char* id, const ActionInfo* actions, size_t count) {
    if (!ImGui::BeginTable(id, 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp)) return;
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 230);
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

}  // namespace

void settings_load() {
    ntr::config_load(paths().config(), g_config);
}

const ntr::Config& settings() { return g_config; }

bool settings_capturing() { return g_capture.active; }

void settings_event(const SDL_Event& ev) {
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

void settings_ui() {
    if (ImGui::CollapsingHeader("Vídeo", ImGuiTreeNodeFlags_DefaultOpen)) {
        int scale = g_config.get_int("video", "window_scale", 1, 16);
        if (ImGui::SliderInt("Tamanho da janela", &scale, 1, 6, "%d×")) set("video", "window_scale", std::to_string(scale));
        ImGui::SameLine();
        ImGui::TextDisabled("(%d×%d)", 256 * scale, 384 * scale);

        int res = g_config.get_int("video", "resolution_3d", 1, 4) - 1;
        const char* kRes[] = {"1× (nativa, 256×192)", "2×", "3×", "4×"};
        if (ImGui::Combo("Resolução do 3D", &res, kRes, 4)) set("video", "resolution_3d", std::to_string(res + 1));

        int filter = SDL_strcasecmp(g_config.get("video", "filter").c_str(), "linear") == 0;
        const char* kFilters[] = {"Nítido (nearest)", "Suavizado (linear)"};
        if (ImGui::Combo("Ampliação", &filter, kFilters, 2)) set("video", "filter", filter ? "linear" : "nearest");

        bool fullscreen = g_config.get_bool("video", "fullscreen");
        if (ImGui::Checkbox("Abrir em tela cheia", &fullscreen)) set("video", "fullscreen", fullscreen ? "true" : "false");
        bool integer = g_config.get_bool("video", "integer_scale");
        if (ImGui::Checkbox("Ampliar só em múltiplos inteiros", &integer)) set("video", "integer_scale", integer ? "true" : "false");
    }
    if (ImGui::CollapsingHeader("Áudio", ImGuiTreeNodeFlags_DefaultOpen)) {
        int volume = g_config.get_int("audio", "volume", 0, 100);
        if (ImGui::SliderInt("Volume", &volume, 0, 100, "%d%%")) set("audio", "volume", std::to_string(volume));
    }
    if (ImGui::CollapsingHeader("Controles", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextWrapped("\"Mudar\" troca pela próxima tecla ou botão que você apertar; \"+\" adiciona mais um; \"x\" desliga.");
        bindings_table("buttons", kButtons, std::size(kButtons));
        ImGui::Spacing();
        ImGui::TextUnformatted("Atalhos");
        bindings_table("shortcuts", kShortcuts, std::size(kShortcuts));
    }
    if (ImGui::CollapsingHeader("Launcher")) {
        bool check = g_config.get_bool("launcher", "check_updates");
        if (ImGui::Checkbox("Procurar atualizações ao abrir", &check)) set("launcher", "check_updates", check ? "true" : "false");
        bool close = g_config.get_bool("launcher", "close_on_play");
        if (ImGui::Checkbox("Fechar o launcher quando o jogo abrir", &close)) set("launcher", "close_on_play", close ? "true" : "false");
    }
    ImGui::Separator();
    if (ImGui::Button("Restaurar o padrão")) ImGui::OpenPopup("Restaurar");
    if (ImGui::BeginPopupModal("Restaurar", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Voltar todas as configurações e controles ao padrão?");
        if (ImGui::Button("Restaurar")) {
            ntr::config_load("", g_config);
            save();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancelar")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s", g_status.c_str());
    capture_popup();
}

}  // namespace launcher
