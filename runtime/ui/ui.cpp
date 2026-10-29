// Shared Dear ImGui screens: the settings panel (boej.ini edited with widgets and saved on
// every change; bindings captured by pressing the key or gamepad input) and the game's
// pause menu around it.
#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <map>
#include <sstream>
#include <vector>

#include <ntr/achievements.h>
#include <ntr/ui.h>

#include "fonts_embedded.h"

namespace ntr::ui {

namespace {

// The panel being drawn: set at the start of settings_panel.
Config* g_config = nullptr;
std::string g_path;
SettingsOptions g_options;
std::string g_status;

struct ActionInfo {
    const char* key;  // in boej.ini
    const char* en;   // for the player, in English and in Portuguese
    const char* pt;
    const char* label() const { return L(en, pt); }
};
const ActionInfo kButtons[] = {
    {"A", "A", "A"}, {"B", "B", "B"}, {"X", "X", "X"}, {"Y", "Y", "Y"}, {"L", "L", "L"}, {"R", "R", "R"},
    {"START", "Start", "Start"}, {"SELECT", "Select", "Select"}, {"UP", "Up", "Cima"}, {"DOWN", "Down", "Baixo"},
    {"LEFT", "Left", "Esquerda"}, {"RIGHT", "Right", "Direita"},
};
const ActionInfo kShortcuts[] = {
    {"menu", "Pause menu", "Menu de pausa"},
    {"fast_forward", "Fast-forward (hold)", "Acelerar (segurar)"},
    {"fast_forward_toggle", "Fast-forward (toggle)", "Acelerar (liga/desliga)"},
    {"turbo", "Auto-A (hold)", "A automático (segurar)"},
    {"turbo_toggle", "Auto-A (toggle)", "A automático (liga/desliga)"},
    {"screenshot", "Screenshot", "Captura de tela"},
    {"debug_dump", "Report a problem", "Registrar problema"},
    {"fullscreen", "Fullscreen", "Tela cheia"},
    {"save_state", "Save savestate", "Salvar savestate"},
    {"load_state", "Load savestate", "Carregar savestate"},
    {"state_slot_prev", "Previous slot", "Slot anterior"},
    {"state_slot_next", "Next slot", "Próximo slot"},
    {"rewind", "Rewind (hold)", "Voltar no tempo (segurar)"},
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
    g_status = config_save(g_path, *g_config) ? L("Saved to boej.ini.", "Salvo no boej.ini.")
                                               : L("Could not write boej.ini.", "Não foi possível gravar o boej.ini.");
    if (g_options.on_change) g_options.on_change();
}

void set(const char* section, const char* key, const std::string& value) {
    g_config->set(section, key, value);
    save();
}

// A slider for a number in boej.ini. It saves and applies the value when it is let go: at
// every step of a drag the window would change size and the menu flicker. While it moves,
// it shows its own value. The label is a printf format for ImGui ("%%" for a percent sign).
void slider_setting(const char* id, const char* section, const char* key, int lo, int hi,
                    const std::function<std::string(int)>& label) {
    static ImGuiID editing = 0;
    static int moving = 0;
    ImGuiID item = ImGui::GetID(id);
    int value = editing == item ? moving : g_config->get_int(section, key, lo, hi);
    ImGui::SliderInt(id, &value, lo, hi, label(value).c_str(), ImGuiSliderFlags_AlwaysClamp);
    if (ImGui::IsItemActive()) {
        editing = item;
        moving = value;
    } else if (editing == item) {
        editing = 0;
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) set(section, key, std::to_string(value));
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
    static const struct {
        const char* name;
        const char* en;
        const char* pt;
    } kNames[] = {
        {"south", "bottom button (A on Xbox, B on Nintendo)", "botão de baixo (A no Xbox, B no Nintendo)"},
        {"east", "right button (B on Xbox, A on Nintendo)", "botão da direita (B no Xbox, A no Nintendo)"},
        {"west", "left button (X on Xbox, Y on Nintendo)", "botão da esquerda (X no Xbox, Y no Nintendo)"},
        {"north", "top button (Y on Xbox, X on Nintendo)", "botão de cima (Y no Xbox, X no Nintendo)"},
    };
    std::string out;
    for (const std::string& item : split(value)) {
        for (const auto& n : kNames) {
            if (SDL_strcasecmp(item.c_str(), n.name) == 0) out += (out.empty() ? "" : "\n") + item + ": " + L(n.en, n.pt);
        }
    }
    return out;
}

// The current text size times `factor`, before the global scale (PushFont applies it).
void push_font_size(float factor) {
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * factor);
}

// The bold font, at the current size times `factor`.
void push_bold(float factor) {
    ImGui::PushFont(bold_font(), ImGui::GetStyle().FontSizeBase * factor);
}

// The panel's accents (ntr/ui.h has the palette).
const ImVec4 kAccent = ImGui::ColorConvertU32ToFloat4(kPrimary);
const ImVec4 kAccentHover = ImGui::ColorConvertU32ToFloat4(kPrimaryHover);
const ImVec4 kAccentActive = ImGui::ColorConvertU32ToFloat4(kPrimaryActive);
const ImVec4 kYellow = ImGui::ColorConvertU32ToFloat4(kHighlight);

// A small tag, like "BETA" beside an option still being tested: its left edge at `pos`,
// centered on the line of text that starts at pos.y. Returns its width.
float draw_chip(ImDrawList* dl, ImVec2 pos, const char* text) {
    float line = ImGui::GetTextLineHeight();
    push_bold(0.62f);
    ImVec2 size = ImGui::CalcTextSize(text);
    float pad = size.y * 0.5f, h = size.y * 1.25f;
    ImVec2 p0(pos.x, pos.y + (line - h) / 2), p1(pos.x + size.x + pad * 2, p0.y + h);
    ImVec4 tint = ImGui::ColorConvertU32ToFloat4(kMagenta);
    tint.w = 0.22f;
    dl->AddRectFilled(p0, p1, ImGui::GetColorU32(tint), h / 2);
    dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(p0.x + pad, p0.y + (h - size.y) / 2), kMagenta, text);
    ImGui::PopFont();
    return p1.x - p0.x;
}

// A short note under a widget: smaller, slanted (the font has no italic: the glyphs'
// vertices are sheared) and a little lighter than the hints.
void italic_note(const char* text) {
    constexpr float kSlant = 0.2f;
    push_font_size(0.88f);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.80f, 0.84f, 0.94f, 1));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    int first = dl->VtxBuffer.Size;
    float top = ImGui::GetCursorScreenPos().y, line = ImGui::GetTextLineHeight();
    ImGui::TextWrapped("%s", text);
    // Glyphs are quads of 4 vertices; each leans from its own line's bottom.
    for (int i = first; i + 3 < dl->VtxBuffer.Size; i += 4) {
        ImDrawVert* v = &dl->VtxBuffer[i];
        float center = (v[0].pos.y + v[2].pos.y) / 2;
        float bottom = top + (std::floor((center - top) / line) + 1) * line;
        for (int k = 0; k < 4; k++) v[k].pos.x += (bottom - v[k].pos.y) * kSlant;
    }
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

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
size_t g_extra = 0;  // which of the caller's pages, with Page::Extra
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
    if (ImGui::Checkbox(value ? L("On", "Ligado") : L("Off", "Desligado"), &value)) set(section, key, value ? "true" : "false");
    ImGui::PopID();
}

void page_title(const char* title, const char* subtitle) {
    push_bold(1.6f);
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
    std::string text = value.empty() ? L("(none)", "(nenhuma)") : pretty_list(value);
    ImGui::PushID(section);
    ImGui::PushID(action.key);
    ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0, 0.5f));
    if (value.empty()) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    if (ImGui::Button(text.c_str(), ImVec2(-FLT_MIN, 0))) g_capture = {true, gamepad, false, action.key, action.label()};
    if (value.empty()) ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    if (ImGui::IsItemHovered()) {
        std::string about = gamepad ? describe_pad(value) : "";
        const char* change = L("Click to change", "Clique para trocar");
        ImGui::SetTooltip("%s", about.empty() ? change : (about + "\n\n" + change).c_str());
    }
    ImGui::PopID();
    ImGui::PopID();
}

void bindings_page(const char* id, const ActionInfo* actions, size_t count) {
    segmented(L("Keyboard", "Teclado"), L("Controller", "Controle"), g_gamepad_view);
    const char* hint = L("Click an action to change it.", "Clique numa ação para trocar.");
    float right_edge = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
    if (ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + ImGui::CalcTextSize(hint).x <= right_edge) {
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
    }
    ImGui::TextDisabled("%s", hint);
    ImGui::Spacing();
    if (!begin_rows(id)) return;
    for (size_t i = 0; i < count; i++) {
        row(actions[i].label());
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
        ImGui::TextDisabled("%s  ·  %s", g_capture.gamepad ? L("Controller", "Controle") : L("Keyboard", "Teclado"),
                            g_capture.label.c_str());
        ImGui::Spacing();
        push_font_size(1.25f);
        ImGui::TextUnformatted(g_capture.gamepad ? L("Press a button or move an axis", "Aperte um botão ou mova um eixo")
                                                  : L("Press a key", "Aperte uma tecla"));
        ImGui::PopFont();
        ImGui::Spacing();
        ImGui::TextDisabled("%s %s", L("Current:", "Atual:"), current.empty() ? L("(none)", "(nenhuma)") : pretty_list(current).c_str());
        ImGui::Checkbox(L("Keep the current ones and add this one", "Manter as atuais e adicionar esta"), &g_capture.append);
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        if (ImGui::Button(L("Remove all", "Remover todas"))) {
            g_capture.active = false;
            set(section, g_capture.key.c_str(), "");
        }
        ImGui::SameLine();
        if (ImGui::Button(L("Cancel (Esc)", "Cancelar (Esc)"))) g_capture.active = false;
        if (!g_capture.active) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

ImFont* g_bold = nullptr;

// Rubik, regular and bold (runtime/ui/fonts_embedded.h). What Rubik lacks (arrows, other
// scripts) comes from a system font merged into both.
void load_font(float scale) {
    ImGuiIO& io = ImGui::GetIO();
    ImFontConfig config;
    config.FontDataOwnedByAtlas = false;  // static data
    auto add = [&](const unsigned char* data, int size) {
        return io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(data), size, 18.0f * scale, &config);
    };
    ImFont* regular = add(kRubikRegular, int(sizeof kRubikRegular));
    g_bold = add(kRubikBold, int(sizeof kRubikBold));
    static const char* const kFallbacks[] = {
        "C:/Windows/Fonts/segoeui.ttf",
        "/usr/share/fonts/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    };
    for (const char* path : kFallbacks) {
        std::error_code ec;
        if (!std::filesystem::exists(path, ec)) continue;
        for (ImFont* font : {regular, g_bold}) {
            ImFontConfig merge;
            merge.MergeMode = true;
            merge.DstFont = font;
            io.Fonts->AddFontFromFileTTF(path, 18.0f * scale, &merge);
        }
        break;
    }
}

// A confirmation popup, opened with OpenPopup(id) and titled `yes`; true when confirmed.
bool confirm(const char* id, const char* text, const char* yes) {
    bool ok = false;
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    std::string title = std::string(yes) + "###" + id;
    if (ImGui::BeginPopupModal(title.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
        ImGui::TextUnformatted(text);
        ImGui::Spacing();
        if (ImGui::Button(yes)) {
            ok = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(L("Cancel", "Cancelar"))) ImGui::CloseCurrentPopup();
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
    c[ImGuiCol_Text] = ImVec4(0.95f, 0.96f, 1, 1);
    c[ImGuiCol_TextDisabled] = ImVec4(0.66f, 0.71f, 0.85f, 1);  // the card's soft blue, dimmed
    c[ImGuiCol_WindowBg] = ImGui::ColorConvertU32ToFloat4(kBackground);
    c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg] = ImVec4(0.08f, 0.10f, 0.21f, 1);
    c[ImGuiCol_Border] = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.35f);
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
    c[ImGuiCol_PlotHistogram] = kAccentHover;
    c[ImGuiCol_NavCursor] = kYellow;
    c[ImGuiCol_TextSelectedBg] = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.45f);
    c[ImGuiCol_ScrollbarGrab] = ImVec4(1, 1, 1, 0.14f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(1, 1, 1, 0.22f);
    c[ImGuiCol_ScrollbarGrabActive] = kAccent;
    c[ImGuiCol_TableHeaderBg] = ImVec4(0.19f, 0.22f, 0.67f, 0.45f);  // the card's band (#3139AC)
    c[ImGuiCol_TableBorderStrong] = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.40f);
    c[ImGuiCol_TableBorderLight] = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.18f);
    c[ImGuiCol_TableRowBgAlt] = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.07f);
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0.55f);
    style.ScaleAllSizes(scale);
    load_font(scale);
    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);
}

ImFont* bold_font() { return g_bold; }

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

// The output pixels per logical unit, from begin_overlay_frame to its end.
float g_overlay_fit = 1;

void begin_overlay_frame(SDL_Renderer* renderer) {
    int lw = 0, lh = 0;
    SDL_RendererLogicalPresentation mode;
    SDL_FRect rect{};
    SDL_GetRenderLogicalPresentation(renderer, &lw, &lh, &mode);
    SDL_GetRenderLogicalPresentationRect(renderer, &rect);
    g_overlay_fit = lw > 0 && rect.w > 0 ? rect.w / float(lw) : 1;
    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    // In output pixels over the game's picture: the render scale 1 / fit undoes the
    // logical presentation's.
    ImGuiIO& io = ImGui::GetIO();
    if (lw > 0) io.DisplaySize = ImVec2(rect.w, rect.h);
    io.DisplayFramebufferScale = ImVec2(1, 1);
    ImGui::NewFrame();
}

void end_overlay_frame(SDL_Renderer* renderer) {
    ImGui::Render();
    SDL_SetRenderScale(renderer, 1 / g_overlay_fit, 1 / g_overlay_fit);
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
    if ((in_game && g_page == Page::Launcher) || (g_page == Page::Extra && g_extra >= options.extra_pages.size())) g_page = Page::Video;
    const ImGuiStyle& style = ImGui::GetStyle();

    // Left: the sections (plus the caller's own items, e.g. the pause menu's actions).
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(1, 1, 1, 0.035f));
    float nav_width = std::min(ImGui::GetFontSize() * 11, ImGui::GetContentRegionAvail().x * 0.36f);
    ImGui::BeginChild("nav", ImVec2(nav_width, 0), ImGuiChildFlags_NavFlattened | ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PopStyleColor();
    if (options.nav_top) options.nav_top();
    ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0, 0.5f));
    for (size_t i = 0; i < options.extra_pages.size(); i++) {
        if (ImGui::Selectable(options.extra_pages[i].name.c_str(), g_page == Page::Extra && g_extra == i, 0,
                              ImVec2(0, ImGui::GetFrameHeight()))) {
            g_page = Page::Extra;
            g_extra = i;
        }
    }
    if (!options.extra_pages.empty()) ImGui::Spacing();
    ImGui::TextDisabled("%s", L("SETTINGS", "CONFIGURAÇÕES"));
    struct Entry {
        Page page;
        const char* label;
    };
    const Entry kPages[] = {{Page::Video, L("Video", "Vídeo")}, {Page::Audio, L("Audio", "Áudio")},
                            {Page::Buttons, L("Controls", "Controles")}, {Page::Shortcuts, L("Shortcuts", "Atalhos")},
                            {Page::Launcher, "Launcher"}};
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
            options.extra_pages[g_extra].draw();
            break;
        case Page::Video: {
            page_title(L("Video", "Vídeo"), nullptr);
            if (!begin_rows("video")) break;
            bool wide = config.get_bool("video", "wide_layout");
            row("Layout", wide ? L("Both screens in a corner and the bottom one enlarged beside them.",
                                   "As duas telas num canto e a de baixo ampliada ao lado.")
                               : L("One screen above the other, like on the DS.", "As duas telas uma sobre a outra, como no DS."));
            int layout = wide ? 0 : 1;
            const char* kLayouts[] = {L("Widescreen (16:9)", "Tela larga (16:9)"), L("Stacked screens", "Telas empilhadas")};
            if (ImGui::Combo("##layout", &layout, kLayouts, 2)) set("video", "wide_layout", layout == 0 ? "true" : "false");
            if (wide) {
                row(L("Small screens", "Telas pequenas"),
                    L("Which side the two screens go on; the enlarged bottom screen takes the other.",
                      "De que lado ficam as duas telas; a de baixo ampliada vai para o outro."));
                int side = SDL_strcasecmp(config.get("video", "wide_side").c_str(), "right") == 0;
                const char* kSides[] = {L("On the left", "À esquerda"), L("On the right", "À direita")};
                if (ImGui::Combo("##side", &side, kSides, 2)) set("video", "wide_side", side ? "right" : "left");
            } else {
                row(L("Window size", "Tamanho da janela"));
                slider_setting("##scale", "video", "window_scale", 1, 6, [](int scale) {
                    char label[48];
                    std::snprintf(label, sizeof label, "%d× (%d×%d)", scale, 256 * scale, 384 * scale);
                    return std::string(label);
                });
                checkbox_row(L("Integer multiples only", "Só múltiplos inteiros"),
                             L("Every pixel the same size; may leave black borders.",
                               "Pixels todos do mesmo tamanho; pode sobrar borda preta."),
                             "video", "integer_scale");
            }
            checkbox_row(in_game ? L("Fullscreen", "Tela cheia") : L("Start in fullscreen", "Abrir em tela cheia"),
                         L("The fullscreen shortcut also toggles it.", "O atalho de tela cheia também alterna."), "video",
                         "fullscreen");
            row(L("3D resolution", "Resolução do 3D"),
                L("Higher is sharper and heavier. Above 4× it needs a processor with several cores.",
                  "Mais alta fica mais nítida e pesa mais. Acima de 4× precisa de um processador com vários núcleos."));
            int res = config.get_int("video", "resolution_3d", 1, 10);
            auto res_label = [](int n) {
                char label[32];
                if (n == 1) std::snprintf(label, sizeof label, "%s", L("1× (native)", "1× (nativa)"));
                else std::snprintf(label, sizeof label, "%d× (%d×%d)", n, 256 * n, 192 * n);
                return std::string(label);
            };
            if (ImGui::BeginCombo("##res3d", res_label(res).c_str())) {
                for (int n = 1; n <= 10; n++) {
                    if (ImGui::Selectable(res_label(n).c_str(), n == res)) set("video", "resolution_3d", std::to_string(n));
                }
                ImGui::EndCombo();
            }
            row(L("Scaling filter", "Filtro de ampliação"),
                L("Sharp smooth keeps the pixels crisp without uneven sizes; LCD adds the DS screen's grid.",
                  "Nítido suave mantém os pixels sem tamanhos desiguais; LCD acrescenta a grade da tela do DS."));
            const char* kFilterValues[] = {"nearest", "linear", "sharp", "lcd"};
            const char* kFilters[] = {L("Sharp (pixels)", "Nítido (pixels)"), L("Smooth", "Suavizado"),
                                      L("Sharp smooth", "Nítido suave"), "LCD"};
            int filter = 0;
            for (int i = 0; i < 4; i++) {
                if (SDL_strcasecmp(config.get("video", "filter").c_str(), kFilterValues[i]) == 0) filter = i;
            }
            if (ImGui::Combo("##filter", &filter, kFilters, 4)) set("video", "filter", kFilterValues[filter]);
            std::string api_hint = L("Takes effect when the game starts again.", "Vale quando o jogo abrir de novo.");
            if (!options.active_renderer.empty()) api_hint += std::string(" ") + L("In use: ", "Em uso: ") + options.active_renderer;
            row(L("Graphics API", "API gráfica"), api_hint.c_str());
            struct ApiOption {
                const char* value;
                const char* name;
                const char* hint;
                bool beta;  // the SDL_GPU ones: nothing uses the GPU beyond showing the picture yet
            };
            // Default first, the others in alphabetical order.
            const ApiOption kApiOptions[] = {
                {"auto", L("Default", "Padrão"),
                 L("Direct3D 11 on Windows, OpenGL on Linux. The most tested.",
                   "Direct3D 11 no Windows, OpenGL no Linux. A mais testada."),
                 false},
#ifdef _WIN32
                {"d3d11", "Direct3D 11", L("The most compatible on Windows.", "A mais compatível no Windows."), false},
                {"d3d12", "Direct3D 12",
                 L("Through SDL_GPU. Windows 10 and 11's own API.", "Pela SDL_GPU. A API do próprio Windows 10 e 11."), true},
#endif
                {"opengl", "OpenGL", L("Works on almost any video card.", "Funciona em quase todas as placas de vídeo."), false},
                {"vulkan", "Vulkan",
                 L("Through SDL_GPU. Needs an up-to-date video driver.", "Pela SDL_GPU. Precisa de um driver de vídeo atualizado."),
                 true},
            };
            int api = 0;
            for (int i = 0; i < int(std::size(kApiOptions)); i++) {
                if (SDL_strcasecmp(config.get("video", "api").c_str(), kApiOptions[i].value) == 0) api = i;
            }
            // The chip goes into the combo's own draw list after the combo, so it is drawn over the frame.
            const ImGuiStyle& st = ImGui::GetStyle();
            ImDrawList* combo_dl = ImGui::GetWindowDrawList();
            ImVec2 combo_pos = ImGui::GetCursorScreenPos();
            bool open = ImGui::BeginCombo("##api", kApiOptions[api].name);
            if (kApiOptions[api].beta) {
                draw_chip(combo_dl,
                          ImVec2(combo_pos.x + st.FramePadding.x + ImGui::CalcTextSize(kApiOptions[api].name).x +
                                     st.ItemInnerSpacing.x * 2,
                                 combo_pos.y + st.FramePadding.y),
                          "BETA");
            }
            if (open) {
                for (int i = 0; i < int(std::size(kApiOptions)); i++) {
                    if (ImGui::Selectable(kApiOptions[i].name, i == api)) set("video", "api", kApiOptions[i].value);
                    if (kApiOptions[i].beta) {
                        draw_chip(ImGui::GetWindowDrawList(),
                                  ImVec2(ImGui::GetItemRectMin().x + ImGui::CalcTextSize(kApiOptions[i].name).x +
                                             st.ItemInnerSpacing.x * 2,
                                         ImGui::GetItemRectMin().y + st.ItemSpacing.y / 2),  // the text's line
                                  "BETA");
                    }
                }
                ImGui::EndCombo();
            }
            italic_note(kApiOptions[api].hint);
            end_rows();
            break;
        }
        case Page::Audio: {
            page_title(L("Audio", "Áudio"), nullptr);
            if (!begin_rows("audio")) break;
            row("Volume");
            slider_setting("##volume", "audio", "volume", 0, 100,
                           [](int volume) { return std::to_string(volume) + "%%"; });
            end_rows();
            break;
        }
        case Page::Buttons:
            page_title(L("Controls", "Controles"), L("The DS buttons.", "Os botões do DS."));
            bindings_page("buttons", kButtons, std::size(kButtons));
            break;
        case Page::Shortcuts:
            page_title(L("Shortcuts", "Atalhos"), L("Extra features of the port.", "Funções extras do port."));
            if (begin_rows("speed")) {
                row(L("Fast-forward speed", "Velocidade ao acelerar"),
                    L("If the processor cannot keep up, it goes as fast as it can.",
                      "Se o processador não aguentar, vai o mais rápido que ele conseguir."));
                int speed = config.get_int("game", "fast_forward_speed", 2, 10);
                auto speed_label = [](int n) { return std::to_string(n) + "×"; };
                if (ImGui::BeginCombo("##ffspeed", speed_label(speed).c_str())) {
                    for (int n = 2; n <= 10; n++) {
                        if (ImGui::Selectable(speed_label(n).c_str(), n == speed)) set("game", "fast_forward_speed", std::to_string(n));
                    }
                    ImGui::EndCombo();
                }
                checkbox_row(L("Rewind", "Voltar no tempo (rewind)"),
                             L("Holding the shortcut takes the game back a few seconds.",
                               "Segurar o atalho volta o jogo alguns segundos."),
                             "game", "rewind");
                if (config.get_bool("game", "rewind")) {
                    row(L("How far back", "Quanto dá para voltar"), L("More time uses more memory.", "Mais tempo usa mais memória."));
                    int seconds = config.get_int("game", "rewind_seconds", 5, 120);
                    auto seconds_label = [](int n) { return std::to_string(n) + L(" seconds", " segundos"); };
                    if (ImGui::BeginCombo("##rewind", seconds_label(seconds).c_str())) {
                        for (int n : {10, 20, 30, 60, 120}) {
                            if (ImGui::Selectable(seconds_label(n).c_str(), n == seconds)) set("game", "rewind_seconds", std::to_string(n));
                        }
                        ImGui::EndCombo();
                    }
                }
                end_rows();
            }
            ImGui::Spacing();
            bindings_page("shortcuts", kShortcuts, std::size(kShortcuts));
            break;
        case Page::Launcher:
            page_title("Launcher", nullptr);
            if (!begin_rows("launcher")) break;
            {
                // Both names in both languages: someone who does not read the current one still finds it.
                row(L("Language (Idioma)", "Idioma (Language)"),
                    L("Of the launcher and the pause menu. The game's own text does not change.",
                      "Do launcher e do menu de pausa. O texto do jogo não muda."));
                int lang = language() == Language::Portuguese;
                const char* kLanguages[] = {"English", "Português (Brasil)"};
                if (ImGui::Combo("##language", &lang, kLanguages, 2)) {
                    set_language(lang ? Language::Portuguese : Language::English);
                    set("ui", "language", lang ? "pt" : "en");
                }
            }
            checkbox_row(L("Check for updates on start", "Procurar atualizações ao abrir"), nullptr, "launcher", "check_updates");
            checkbox_row(L("Close the launcher when the game starts", "Fechar o launcher ao abrir o jogo"), nullptr, "launcher",
                         "close_on_play");
            if (config.get_bool("launcher", "close_on_play"))
                checkbox_row(L("Do not reopen when the game closes", "Não reabrir ao fechar o jogo"), nullptr, "launcher",
                             "stay_closed");
            end_rows();
            break;
    }
    ImGui::EndChild();
    ImGui::Separator();
    if (g_page == Page::Extra) {  // not a settings page: no footer
        ImGui::EndGroup();
        return;
    }
    if (ImGui::Button(L("Restore defaults", "Restaurar o padrão"))) ImGui::OpenPopup("restore");
    if (confirm("restore", L("Reset all settings and controls to the defaults?", "Voltar todas as configurações e controles ao padrão?"),
                L("Restore", "Restaurar"))) {
        std::string lang = config.get("ui", "language");  // the language is not a setting to reset by accident
        config_load("", config);
        config.set("ui", "language", lang);
        save();
    }
    const char* status = g_status.empty() ? L("Everything is saved right away.", "Tudo é salvo na hora.") : g_status.c_str();
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
    ImVec2 size(std::min(vp->WorkSize.x * 0.92f, em * 64), std::min(vp->WorkSize.y * 0.92f, em * 36));
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);
    ImGui::Begin("pause", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);

    SettingsOptions menu = options;
    menu.nav_top = [&] {
        push_bold(1.5f);
        ImGui::TextUnformatted(L("Paused", "Pausado"));
        ImGui::PopFont();
        ImGui::Spacing();
        push_accent();
        if (ImGui::Button(L("Resume", "Continuar"), ImVec2(-FLT_MIN, ImGui::GetFrameHeight() * 1.3f))) action = PauseAction::Resume;
        pop_accent();
        ImGui::Spacing();
        ImGui::Spacing();
    };
    menu.nav_bottom_height = 2;
    menu.nav_bottom = [&] {
        if (ImGui::Button(L("Reset", "Reiniciar"), ImVec2(-FLT_MIN, 0))) ImGui::OpenPopup("restart");
        if (ImGui::Button(L("Quit game", "Sair do jogo"), ImVec2(-FLT_MIN, 0))) ImGui::OpenPopup("quit");
        if (confirm("restart",
                    L("Go back to the title screen? Progress since the last save will be lost.",
                      "Voltar à tela de título? O progresso desde o último save será perdido."),
                    L("Reset", "Reiniciar"))) {
            action = PauseAction::Restart;
        }
        if (confirm("quit",
                    L("Close the game? Progress since the last save will be lost.",
                      "Fechar o jogo? O progresso desde o último save será perdido."),
                    L("Quit", "Sair"))) {
            action = PauseAction::Quit;
        }
    };
    menu.extra_pages.insert(menu.extra_pages.begin(), {"Savestates", [&] {
        page_title("Savestates", L("The game exactly as it is now, to come back to this point later.",
                                   "O jogo exatamente como está agora, para voltar a este ponto depois."));
        if (!begin_rows("states")) return;
        for (size_t i = 0; i < states.size(); i++) {
            std::string name = "Slot " + std::to_string(i + 1);
            row(name.c_str(), states[i].empty() ? L("Empty", "Vazio") : states[i].c_str());
            ImGui::PushID(int(i));
            float w = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2;
            if (ImGui::Button(L("Save", "Salvar"), ImVec2(w, 0))) {
                if (states[i].empty()) {
                    action = PauseAction::SaveState;
                    slot = int(i) + 1;
                } else {
                    ImGui::OpenPopup("replace");
                }
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(!loadable[i]);
            if (ImGui::Button(L("Load", "Carregar"), ImVec2(w, 0))) {
                action = PauseAction::LoadState;
                slot = int(i) + 1;
            }
            ImGui::EndDisabled();
            std::string question = L("Replace the savestate in ", "Substituir o savestate do ") + name + "?";
            if (confirm("replace", question.c_str(), L("Replace", "Substituir"))) {
                action = PauseAction::SaveState;
                slot = int(i) + 1;
            }
            ImGui::PopID();
        }
        end_rows();
        ImGui::Spacing();
        ImGui::TextWrapped("%s", L("Each slot is a file in the savestates folder, next to the game. The shortcuts (under "
                                   "Shortcuts) save and load the chosen slot. Loading does not change the game's own save.",
                                   "Cada slot é um arquivo na pasta savestates, ao lado do jogo. Os atalhos (em Atalhos) "
                                   "salvam e carregam no slot escolhido. O save do próprio jogo não é alterado ao carregar."));
    }});
    settings_panel(config, path, menu);
    ImGui::End();
    return action;
}

// --- achievements --------------------------------------------------------------------------

SDL_Texture* png_texture(SDL_Renderer* renderer, const std::string& path) {
    static std::map<std::string, SDL_Texture*> cache;  // nullptr: could not be read
    auto it = cache.find(path);
    if (it != cache.end()) return it->second;
    SDL_Texture*& texture = cache[path];
    if (path.empty()) return nullptr;
    SDL_Surface* surface = SDL_LoadPNG(path.c_str());
    if (!surface) {
        NTR_LOG("[ui] cannot load %s: %s\n", path.c_str(), SDL_GetError());
        return nullptr;
    }
    texture = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_DestroySurface(surface);
    if (texture) SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR);
    return texture;
}

namespace {

ImTextureRef texture_ref(SDL_Texture* texture) { return ImTextureRef(ImTextureID(intptr_t(texture))); }

// "2026-10-29T14:03:12-03:00" as "29/10/2026 14:03" (Portuguese) or "2026-10-29 14:03".
std::string date_text(const std::string& iso) {
    if (iso.size() < 16) return iso;
    if (language() == Language::Portuguese)
        return iso.substr(8, 2) + "/" + iso.substr(5, 2) + "/" + iso.substr(0, 4) + " " + iso.substr(11, 5);
    return iso.substr(0, 10) + " " + iso.substr(11, 5);
}

// The icon at `pos`, `size` square, dimmed when locked; a plain square without one.
void icon_at(ImDrawList* dl, SDL_Texture* icon, ImVec2 pos, float size, bool unlocked, float alpha = 1) {
    ImVec2 end(pos.x + size, pos.y + size);
    float rounding = size * 0.12f;
    if (!icon) {
        dl->AddRectFilled(pos, end, ImGui::GetColorU32(ImVec4(1, 1, 1, 0.08f * alpha)), rounding);
        dl->AddText(nullptr, size * 0.5f, ImVec2(pos.x + size * 0.36f, pos.y + size * 0.22f),
                    ImGui::GetColorU32(ImVec4(1, 1, 1, 0.35f * alpha)), "?");
        return;
    }
    ImU32 tint = unlocked ? ImGui::GetColorU32(ImVec4(1, 1, 1, alpha)) : ImGui::GetColorU32(ImVec4(0.45f, 0.45f, 0.45f, 0.55f * alpha));
    dl->AddImageRounded(texture_ref(icon), pos, end, ImVec2(0, 0), ImVec2(1, 1), tint, rounding);
}

}  // namespace

void achievements_list(const Achievements& achievements, SDL_Renderer* renderer) {
    const auto& list = achievements.list();
    if (list.empty()) {
        ImGui::TextWrapped("%s", L("No achievements found (achievements/achievements.json next to the game).",
                                   "Nenhuma conquista encontrada (achievements/achievements.json ao lado do jogo)."));
        return;
    }
    int got = achievements.unlocked_count(), total = int(list.size());
    char count[64];
    std::snprintf(count, sizeof count, L("%d of %d unlocked", "%d de %d desbloqueadas"), got, total);
    ImGui::TextUnformatted(count);
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, kAccent);
    ImGui::ProgressBar(float(got) / float(total), ImVec2(-FLT_MIN, ImGui::GetFontSize() * 0.45f), "");
    ImGui::PopStyleColor();
    ImGui::Spacing();

    // Unlocked first, the latest on top; then the locked ones in their order.
    std::vector<const Achievement*> order;
    for (const Achievement& a : list) order.push_back(&a);
    std::stable_sort(order.begin(), order.end(), [&](const Achievement* x, const Achievement* y) {
        const AchievementStatus &sx = achievements.status(x->id), &sy = achievements.status(y->id);
        if (sx.unlocked != sy.unlocked) return sx.unlocked;
        return sx.unlocked && sx.unlocked_at > sy.unlocked_at;
    });
    const float size = ImGui::GetFontSize() * 2.6f;
    if (!ImGui::BeginTable("achievements", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_PadOuterX)) return;
    ImGui::TableSetupColumn("icon", ImGuiTableColumnFlags_WidthFixed, size);
    ImGui::TableSetupColumn("text", ImGuiTableColumnFlags_WidthStretch);
    for (const Achievement* a : order) {
        const AchievementStatus& st = achievements.status(a->id);
        bool secret = a->hidden && !st.unlocked;
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImVec2 pos = ImGui::GetCursorScreenPos();
        icon_at(ImGui::GetWindowDrawList(), secret ? nullptr : png_texture(renderer, achievements.icon_path(*a)), pos, size, st.unlocked);
        ImGui::Dummy(ImVec2(size, size));
        ImGui::TableNextColumn();
        if (!st.unlocked) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextUnformatted(secret ? "???" : a->name().c_str());
        if (!st.unlocked) ImGui::PopStyleColor();
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("%s", secret ? L("Secret achievement.", "Conquista secreta.") : a->description().c_str());
        ImGui::PopStyleColor();
        if (st.unlocked) {
            std::string when = L("Unlocked ", "Desbloqueada em ") + date_text(st.unlocked_at);
            ImGui::TextColored(kAccentHover, "%s", when.c_str());
        }
    }
    ImGui::EndTable();
}

void achievement_popup(const Achievement& achievement, SDL_Renderer* renderer, const std::string& icon_path, float shown) {
    const ImGuiIO& io = ImGui::GetIO();
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImFont* font = ImGui::GetFont();
    const float text = std::clamp(io.DisplaySize.y / 34, 13.0f, 40.0f), small = text * 0.8f;
    const float pad = text * 0.6f, icon = text * 2.4f;
    const char* title = L("Achievement unlocked!", "Conquista desbloqueada!");
    const std::string& name = achievement.name();
    float text_w = std::max(font->CalcTextSizeA(small, FLT_MAX, 0, title).x, font->CalcTextSizeA(text, FLT_MAX, 0, name.c_str()).x);
    float w = std::min(icon + text_w + pad * 3, io.DisplaySize.x - pad * 2), h = icon + pad * 2;
    // Slides in from the top right corner.
    ImVec2 p0(io.DisplaySize.x - w - pad, pad - (1 - shown) * (h + pad));
    ImVec2 p1(p0.x + w, p0.y + h);
    auto color = [&](float r, float g, float b, float a) { return ImGui::GetColorU32(ImVec4(r, g, b, a * shown)); };
    dl->AddRectFilled(p0, p1, color(0.07f, 0.08f, 0.10f, 0.92f), text * 0.4f);
    dl->AddRect(p0, p1, color(kAccent.x, kAccent.y, kAccent.z, 1), text * 0.4f, 0, std::max(1.0f, text / 10));
    icon_at(dl, png_texture(renderer, icon_path), ImVec2(p0.x + pad, p0.y + pad), icon, true, shown);
    float x = p0.x + pad * 2 + icon, block = small + text * 1.15f, y = p0.y + (h - block) / 2;
    dl->PushClipRect(p0, ImVec2(p1.x - pad, p1.y), true);
    dl->AddText(font, small, ImVec2(x, y), color(1.0f, 0.82f, 0.30f, 1), title);
    dl->AddText(font, text, ImVec2(x, y + small * 1.15f), color(1, 1, 1, 1), name.c_str());
    dl->PopClipRect();
}

}  // namespace ntr::ui
