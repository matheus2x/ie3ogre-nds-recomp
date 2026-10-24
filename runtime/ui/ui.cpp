// Shared Dear ImGui screens: the settings panel (boej.ini edited with widgets and saved on
// every change; bindings captured by pressing the key or gamepad input) and the game's
// pause menu around it.
#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

#include <algorithm>
#include <cfloat>
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
    {"debug_dump", "Registrar problema"},
    {"fullscreen", "Tela cheia"},
    {"save_state", "Salvar savestate"},
    {"load_state", "Carregar savestate"},
    {"state_slot_prev", "Slot anterior"},
    {"state_slot_next", "Próximo slot"},
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
    g_status = config_save(g_path, *g_config) ? "Salvo no boej.ini." : "Não foi possível gravar o boej.ini.";
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

// The current text size times `factor`, before the global scale (PushFont applies it).
void push_font_size(float factor) {
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * factor);
}

// Palette shared by the panel's accents.
const ImVec4 kAccent(0.20f, 0.52f, 0.90f, 1);
const ImVec4 kAccentHover(0.28f, 0.60f, 0.96f, 1);
const ImVec4 kAccentActive(0.15f, 0.44f, 0.80f, 1);

void push_accent() {
    ImGui::PushStyleColor(ImGuiCol_Button, kAccent);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kAccentHover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, kAccentActive);
}

void pop_accent() { ImGui::PopStyleColor(3); }

// "Right Shift, Backspace" -> "Right Shift  /  Backspace".
std::string pretty_list(const std::string& value) {
    std::string out;
    for (const std::string& item : split(value)) out += (out.empty() ? "" : "  /  ") + item;
    return out;
}

// The page being shown; kept between frames and between the launcher's tabs.
enum class Page { Extra, Video, Audio, Buttons, Shortcuts, Launcher };
Page g_page = Page::Video;
bool g_gamepad_view = false;  // the controls pages show the keyboard or the gamepad bindings

// A settings row: the label (and a short hint under it) on the left, the widget on the
// right, filling its column. Rows live in a two-column table opened by begin_rows; in a
// narrow window (the stacked layout's) the widget goes under the label instead.
bool g_narrow = false;

bool begin_rows(const char* id) {
    g_narrow = ImGui::GetContentRegionAvail().x < ImGui::GetFontSize() * 22;
    if (!ImGui::BeginTable(id, g_narrow ? 1 : 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_PadOuterX)) return false;
    ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthStretch, 0.42f);
    if (!g_narrow) ImGui::TableSetupColumn("widget", ImGuiTableColumnFlags_WidthStretch, 0.58f);
    return true;
}

void row(const char* label, const char* hint = nullptr) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    if (!g_narrow) ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    if (hint) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("%s", hint);
        ImGui::PopStyleColor();
    }
    if (!g_narrow) ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-FLT_MIN);
}

void end_rows() {
    ImGui::EndTable();
}

void checkbox_row(const char* label, const char* hint, const char* section, const char* key) {
    row(label, hint);
    bool value = g_config->get_bool(section, key);
    ImGui::PushID(key);
    if (ImGui::Checkbox(value ? "Ligado" : "Desligado", &value)) set(section, key, value ? "true" : "false");
    ImGui::PopID();
}

void page_title(const char* title, const char* subtitle) {
    push_font_size(1.6f);
    ImGui::TextUnformatted(title);
    ImGui::PopFont();
    if (subtitle) ImGui::TextDisabled("%s", subtitle);
    ImGui::Spacing();
    ImGui::Spacing();
}

// Two buttons acting as one switch; true when `value` changed.
bool segmented(const char* a, const char* b, bool& value) {
    bool changed = false;
    float w = ImGui::GetFontSize() * 7;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2, ImGui::GetStyle().ItemSpacing.y));
    for (int i = 0; i < 2; i++) {
        bool on = value == (i == 1);
        if (on) push_accent();
        if (ImGui::Button(i ? b : a, ImVec2(w, 0)) && !on) {
            value = i == 1;
            changed = true;
        }
        if (on) pop_accent();
        if (i == 0) ImGui::SameLine();
    }
    ImGui::PopStyleVar();
    return changed;
}

// One binding: a button showing what is bound; clicking it waits for the new input.
void binding_button(const ActionInfo& action, bool gamepad) {
    const char* section = gamepad ? "gamepad" : "keyboard";
    std::string value = g_config->get(section, action.key);
    std::string text = value.empty() ? "(nenhuma)" : pretty_list(value);
    ImGui::PushID(section);
    ImGui::PushID(action.key);
    ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0, 0.5f));
    if (value.empty()) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    if (ImGui::Button(text.c_str(), ImVec2(-FLT_MIN, 0))) g_capture = {true, gamepad, false, action.key, action.label};
    if (value.empty()) ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    if (ImGui::IsItemHovered()) {
        std::string about = gamepad ? describe_pad(value) : "";
        ImGui::SetTooltip("%s", about.empty() ? "Clique para trocar" : (about + "\n\nClique para trocar").c_str());
    }
    ImGui::PopID();
    ImGui::PopID();
}

void bindings_page(const char* id, const ActionInfo* actions, size_t count) {
    segmented("Teclado", "Controle", g_gamepad_view);
    const char* hint = "Clique numa ação para trocar.";
    float right_edge = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
    if (ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + ImGui::CalcTextSize(hint).x <= right_edge) {
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
    }
    ImGui::TextDisabled("%s", hint);
    ImGui::Spacing();
    if (!begin_rows(id)) return;
    for (size_t i = 0; i < count; i++) {
        row(actions[i].label);
        binding_button(actions[i], g_gamepad_view);
    }
    end_rows();
}

void capture_popup() {
    if (g_capture.active && !ImGui::IsPopupOpen("Capturar")) ImGui::OpenPopup("Capturar");
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(ImGui::GetFontSize() * 20, 0), ImVec2(FLT_MAX, FLT_MAX));
    if (ImGui::BeginPopupModal("Capturar", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar)) {
        const char* section = g_capture.gamepad ? "gamepad" : "keyboard";
        std::string current = g_config ? g_config->get(section, g_capture.key) : "";
        ImGui::TextDisabled("%s  ·  %s", g_capture.gamepad ? "Controle" : "Teclado", g_capture.label.c_str());
        ImGui::Spacing();
        push_font_size(1.25f);
        ImGui::TextUnformatted(g_capture.gamepad ? "Aperte um botão ou mova um eixo" : "Aperte uma tecla");
        ImGui::PopFont();
        ImGui::Spacing();
        ImGui::TextDisabled("Atual: %s", current.empty() ? "(nenhuma)" : pretty_list(current).c_str());
        ImGui::Checkbox("Manter as atuais e adicionar esta", &g_capture.append);
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        if (ImGui::Button("Remover todas")) {
            g_capture.active = false;
            set(section, g_capture.key.c_str(), "");
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancelar (Esc)")) g_capture.active = false;
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
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowPadding = ImVec2(14, 14);
    style.FramePadding = ImVec2(10, 6);
    style.ItemSpacing = ImVec2(10, 8);
    style.CellPadding = ImVec2(8, 6);
    style.ScrollbarSize = 12;
    style.WindowRounding = style.PopupRounding = 8;
    style.ChildRounding = style.FrameRounding = style.TabRounding = 6;
    style.GrabRounding = style.ScrollbarRounding = 4;
    style.WindowBorderSize = 0;
    style.PopupBorderSize = 1;
    ImVec4* c = style.Colors;
    c[ImGuiCol_WindowBg] = ImVec4(0.09f, 0.10f, 0.12f, 1);
    c[ImGuiCol_PopupBg] = ImVec4(0.12f, 0.13f, 0.16f, 1);
    c[ImGuiCol_FrameBg] = ImVec4(1, 1, 1, 0.06f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(1, 1, 1, 0.10f);
    c[ImGuiCol_FrameBgActive] = ImVec4(1, 1, 1, 0.14f);
    c[ImGuiCol_Button] = ImVec4(1, 1, 1, 0.07f);
    c[ImGuiCol_ButtonHovered] = ImVec4(1, 1, 1, 0.13f);
    c[ImGuiCol_ButtonActive] = ImVec4(1, 1, 1, 0.18f);
    c[ImGuiCol_Header] = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.55f);
    c[ImGuiCol_HeaderHovered] = ImVec4(1, 1, 1, 0.08f);
    c[ImGuiCol_HeaderActive] = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.70f);
    c[ImGuiCol_CheckMark] = c[ImGuiCol_SliderGrab] = kAccentHover;
    c[ImGuiCol_SliderGrabActive] = kAccent;
    c[ImGuiCol_Tab] = ImVec4(1, 1, 1, 0.04f);
    c[ImGuiCol_TabHovered] = ImVec4(1, 1, 1, 0.12f);
    c[ImGuiCol_TabSelected] = kAccentActive;
    c[ImGuiCol_TabSelectedOverline] = kAccentHover;
    c[ImGuiCol_Separator] = ImVec4(1, 1, 1, 0.08f);
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0.55f);
    style.ScaleAllSizes(scale);
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
    if ((in_game && g_page == Page::Launcher) || (g_page == Page::Extra && !options.extra_page)) g_page = Page::Video;
    const ImGuiStyle& style = ImGui::GetStyle();

    // Left: the sections (plus the caller's own items, e.g. the pause menu's actions).
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(1, 1, 1, 0.035f));
    float nav_width = std::min(ImGui::GetFontSize() * 11, ImGui::GetContentRegionAvail().x * 0.36f);
    ImGui::BeginChild("nav", ImVec2(nav_width, 0), ImGuiChildFlags_NavFlattened | ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PopStyleColor();
    if (options.nav_top) options.nav_top();
    ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0, 0.5f));
    if (options.extra_page) {
        if (ImGui::Selectable(options.extra_page, g_page == Page::Extra, 0, ImVec2(0, ImGui::GetFrameHeight()))) {
            g_page = Page::Extra;
        }
        ImGui::Spacing();
    }
    ImGui::TextDisabled("CONFIGURAÇÕES");
    struct Entry {
        Page page;
        const char* label;
    };
    const Entry kPages[] = {{Page::Video, "Vídeo"}, {Page::Audio, "Áudio"}, {Page::Buttons, "Controles"},
                            {Page::Shortcuts, "Atalhos"}, {Page::Launcher, "Launcher"}};
    for (const Entry& e : kPages) {
        if (in_game && e.page == Page::Launcher) continue;
        if (ImGui::Selectable(e.label, g_page == e.page, 0, ImVec2(0, ImGui::GetFrameHeight()))) g_page = e.page;
    }
    ImGui::PopStyleVar();
    if (options.nav_bottom) {
        float h = options.nav_bottom_height * ImGui::GetFrameHeightWithSpacing();
        ImGui::SetCursorPosY(std::max(ImGui::GetCursorPosY(), ImGui::GetContentRegionMax().y - h));
        options.nav_bottom();
    }
    ImGui::EndChild();
    ImGui::SameLine(0, style.ItemSpacing.x * 2);

    // Right: the page, with the footer (restore defaults, save status) under it.
    ImGui::BeginGroup();
    float footer = ImGui::GetFrameHeightWithSpacing() + style.ItemSpacing.y;
    ImGui::BeginChild("page", ImVec2(0, -footer), ImGuiChildFlags_NavFlattened);
    switch (g_page) {
        case Page::Extra:
            options.draw_extra_page();
            break;
        case Page::Video: {
            page_title("Vídeo", nullptr);
            if (!begin_rows("video")) break;
            bool wide = config.get_bool("video", "wide_layout");
            row("Layout", wide ? "As duas telas num canto e a de baixo ampliada ao lado."
                               : "As duas telas uma sobre a outra, como no DS.");
            int layout = wide ? 0 : 1;
            const char* kLayouts[] = {"Tela larga (16:9)", "Telas empilhadas"};
            if (ImGui::Combo("##layout", &layout, kLayouts, 2)) set("video", "wide_layout", layout == 0 ? "true" : "false");
            if (wide) {
                row("Telas pequenas", "De que lado ficam as duas telas; a de baixo ampliada vai para o outro.");
                int side = SDL_strcasecmp(config.get("video", "wide_side").c_str(), "right") == 0;
                const char* kSides[] = {"À esquerda", "À direita"};
                if (ImGui::Combo("##side", &side, kSides, 2)) set("video", "wide_side", side ? "right" : "left");
            } else {
                row("Tamanho da janela");
                int scale = config.get_int("video", "window_scale", 1, 16);
                char format[48];
                std::snprintf(format, sizeof format, "%%d× (%d×%d)", 256 * scale, 384 * scale);
                if (ImGui::SliderInt("##scale", &scale, 1, 6, format)) set("video", "window_scale", std::to_string(scale));
                checkbox_row("Só múltiplos inteiros", "Pixels todos do mesmo tamanho; pode sobrar borda preta.", "video",
                             "integer_scale");
            }
            checkbox_row(in_game ? "Tela cheia" : "Abrir em tela cheia", "O atalho de tela cheia também alterna.", "video", "fullscreen");
            row("Resolução do 3D",
                "Mais alta fica mais nítida e pesa mais. Acima de 4× precisa de um processador com vários núcleos.");
            int res = config.get_int("video", "resolution_3d", 1, 10);
            auto res_label = [](int n) {
                char label[32];
                if (n == 1) std::snprintf(label, sizeof label, "1× (nativa)");
                else std::snprintf(label, sizeof label, "%d× (%d×%d)", n, 256 * n, 192 * n);
                return std::string(label);
            };
            if (ImGui::BeginCombo("##res3d", res_label(res).c_str())) {
                for (int n = 1; n <= 10; n++) {
                    if (ImGui::Selectable(res_label(n).c_str(), n == res)) set("video", "resolution_3d", std::to_string(n));
                }
                ImGui::EndCombo();
            }
            row("Filtro de ampliação");
            int filter = SDL_strcasecmp(config.get("video", "filter").c_str(), "linear") == 0;
            const char* kFilters[] = {"Nítido (pixels)", "Suavizado"};
            if (ImGui::Combo("##filter", &filter, kFilters, 2)) set("video", "filter", filter ? "linear" : "nearest");
            end_rows();
            break;
        }
        case Page::Audio: {
            page_title("Áudio", nullptr);
            if (!begin_rows("audio")) break;
            row("Volume");
            int volume = config.get_int("audio", "volume", 0, 100);
            if (ImGui::SliderInt("##volume", &volume, 0, 100, "%d%%")) set("audio", "volume", std::to_string(volume));
            end_rows();
            break;
        }
        case Page::Buttons:
            page_title("Controles", "Os botões do DS.");
            bindings_page("buttons", kButtons, std::size(kButtons));
            break;
        case Page::Shortcuts:
            page_title("Atalhos", "Funções extras do port.");
            if (begin_rows("speed")) {
                row("Velocidade ao acelerar", "Se o processador não aguentar, vai o mais rápido que ele conseguir.");
                int speed = config.get_int("game", "fast_forward_speed", 2, 10);
                auto speed_label = [](int n) { return std::to_string(n) + "×"; };
                if (ImGui::BeginCombo("##ffspeed", speed_label(speed).c_str())) {
                    for (int n = 2; n <= 10; n++) {
                        if (ImGui::Selectable(speed_label(n).c_str(), n == speed)) set("game", "fast_forward_speed", std::to_string(n));
                    }
                    ImGui::EndCombo();
                }
                end_rows();
            }
            ImGui::Spacing();
            bindings_page("shortcuts", kShortcuts, std::size(kShortcuts));
            break;
        case Page::Launcher:
            page_title("Launcher", nullptr);
            if (!begin_rows("launcher")) break;
            checkbox_row("Procurar atualizações ao abrir", nullptr, "launcher", "check_updates");
            checkbox_row("Fechar o launcher ao abrir o jogo", nullptr, "launcher", "close_on_play");
            if (config.get_bool("launcher", "close_on_play"))
                checkbox_row("Não reabrir ao fechar o jogo", nullptr, "launcher", "stay_closed");
            end_rows();
            break;
    }
    ImGui::EndChild();
    ImGui::Separator();
    if (g_page == Page::Extra) {  // not a settings page: no footer
        ImGui::EndGroup();
        return;
    }
    if (ImGui::Button("Restaurar o padrão")) ImGui::OpenPopup("Restaurar");
    if (confirm("Restaurar", "Voltar todas as configurações e controles ao padrão?", "Restaurar")) {
        config_load("", config);
        save();
    }
    const char* status = g_status.empty() ? "Tudo é salvo na hora." : g_status.c_str();
    ImGui::SameLine();
    if (ImGui::CalcTextSize(status).x <= ImGui::GetContentRegionAvail().x) {  // left out when it does not fit
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", status);
    }
    ImGui::EndGroup();
    capture_popup();
}

PauseAction pause_menu(Config& config, const std::string& path, const SettingsOptions& options,
                       const std::vector<std::string>& states, const std::vector<bool>& loadable, int& slot) {
    PauseAction action = PauseAction::None;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    // The menu takes about three quarters of the window's height: in a big window the text
    // grows with it (from the next frame on), instead of a small box in the middle.
    ImGuiStyle& style = ImGui::GetStyle();
    style.FontScaleMain = std::clamp(vp->WorkSize.y * 0.75f / 36 / style.FontSizeBase, 1.0f, 3.0f);
    float em = ImGui::GetFontSize();
    ImVec2 size(std::min(vp->WorkSize.x * 0.92f, em * 52), std::min(vp->WorkSize.y * 0.92f, em * 36));
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);
    ImGui::Begin("Jogo pausado", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);

    SettingsOptions menu = options;
    menu.nav_top = [&] {
        push_font_size(1.5f);
        ImGui::TextUnformatted("Pausado");
        ImGui::PopFont();
        ImGui::Spacing();
        push_accent();
        if (ImGui::Button("Continuar", ImVec2(-FLT_MIN, ImGui::GetFrameHeight() * 1.3f))) action = PauseAction::Resume;
        pop_accent();
        ImGui::Spacing();
        ImGui::Spacing();
    };
    menu.nav_bottom_height = 2;
    menu.nav_bottom = [&] {
        if (ImGui::Button("Reiniciar", ImVec2(-FLT_MIN, 0))) ImGui::OpenPopup("Reiniciar");
        if (ImGui::Button("Sair do jogo", ImVec2(-FLT_MIN, 0))) ImGui::OpenPopup("Sair");
        if (confirm("Reiniciar", "Voltar à tela de título? O progresso desde o último save será perdido.", "Reiniciar")) {
            action = PauseAction::Restart;
        }
        if (confirm("Sair", "Fechar o jogo? O progresso desde o último save será perdido.", "Sair")) action = PauseAction::Quit;
    };
    menu.extra_page = "Savestates";
    menu.draw_extra_page = [&] {
        page_title("Savestates", "O jogo exatamente como está agora, para voltar a este ponto depois.");
        if (!begin_rows("states")) return;
        for (size_t i = 0; i < states.size(); i++) {
            std::string name = "Slot " + std::to_string(i + 1);
            row(name.c_str(), states[i].empty() ? "Vazio" : states[i].c_str());
            ImGui::PushID(int(i));
            float w = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2;
            if (ImGui::Button("Salvar", ImVec2(w, 0))) {
                if (states[i].empty()) {
                    action = PauseAction::SaveState;
                    slot = int(i) + 1;
                } else {
                    ImGui::OpenPopup("Substituir");
                }
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(!loadable[i]);
            if (ImGui::Button("Carregar", ImVec2(w, 0))) {
                action = PauseAction::LoadState;
                slot = int(i) + 1;
            }
            ImGui::EndDisabled();
            std::string question = "Substituir o savestate do " + name + "?";
            if (confirm("Substituir", question.c_str(), "Substituir")) {
                action = PauseAction::SaveState;
                slot = int(i) + 1;
            }
            ImGui::PopID();
        }
        end_rows();
        ImGui::Spacing();
        ImGui::TextWrapped("Cada slot é um arquivo na pasta savestates, ao lado do jogo. Os atalhos (em Atalhos) "
                           "salvam e carregam no slot escolhido. O save do próprio jogo não é alterado ao carregar.");
    };
    settings_panel(config, path, menu);
    ImGui::End();
    return action;
}

}  // namespace ntr::ui
