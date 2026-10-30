// The Gallery: the game's screenshots (screenshots/ next to the launcher, PNGs made by the
// screenshot key, F12 by default; BMPs put there are shown too), newest first, as a grid of
// thumbnails. A click opens the picture large, with the previous and next ones.
//
// Thumbnails are made a few per frame (a screenshot can be 2560x3840 with the 3D at 10x) and
// kept while the file does not change; the large picture is loaded only when opened.
#include <SDL3/SDL.h>
#include <imgui.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <map>

#include <ntr/ui.h>

#include "launcher.h"

namespace fs = std::filesystem;

namespace launcher {

namespace {

struct Shot {
    std::string file;  // the name in screenshots/
    std::string when;  // for the player
    int64_t stamp = 0; // the last write time, to notice a change
    int64_t second = 0;  // the same in seconds, to sort
    int kind = 0;        // the game's three of one key press: 0 both screens, 1 -ampliada, 2 -completa
};

struct Thumb {
    SDL_Texture* texture = nullptr;  // nullptr: not a picture SDL reads
    int64_t stamp = 0;
    float aspect = 1;  // width / height
};

std::vector<Shot> g_shots;
std::string g_listing;
double g_listing_time = -10;
std::map<std::string, Thumb> g_thumbs;
int g_open = -1;  // the picture shown large, an index in g_shots
SDL_Texture* g_large = nullptr;
std::string g_large_file;
int g_large_w = 0, g_large_h = 0;

constexpr int kThumbHeight = 300;  // pixels of a thumbnail texture

bool is_picture(const std::string& name) {
    std::string ext = ntr::utf8_string(ntr::fs_path(name).extension());
    for (char& c : ext) c = char(std::tolower(uint8_t(c)));
    return ext == ".png" || ext == ".bmp";
}

SDL_Surface* load_picture(const std::string& file) {
    std::string path = paths().screenshots() + file;
    std::string ext = ntr::utf8_string(ntr::fs_path(file).extension());
    for (char& c : ext) c = char(std::tolower(uint8_t(c)));
    return ext == ".png" ? SDL_LoadPNG(path.c_str()) : SDL_LoadBMP(path.c_str());
}

void scan() {
    std::error_code ec;
    std::vector<Shot> shots;
    std::string listing;
    for (const auto& entry : fs::directory_iterator(ntr::fs_path(paths().screenshots()), ec)) {
        std::string name = ntr::utf8_string(entry.path().filename());
        if (!entry.is_regular_file(ec) || !is_picture(name) || name[0] == '.') continue;
        Shot s;
        s.file = name;
        auto time = entry.last_write_time(ec);
        s.stamp = int64_t(time.time_since_epoch().count());
        auto sys = std::chrono::system_clock::now() +
                   std::chrono::duration_cast<std::chrono::system_clock::duration>(time - fs::file_time_type::clock::now());
        s.second = std::chrono::duration_cast<std::chrono::seconds>(sys.time_since_epoch()).count();
        std::string stem = ntr::utf8_string(ntr::fs_path(name).stem());
        auto ends = [&](const std::string& suffix) { return stem.size() > suffix.size() && stem.compare(stem.size() - suffix.size(), suffix.size(), suffix) == 0; };
        s.kind = ends("-ampliada") ? 1 : ends("-completa") ? 2 : 0;
        std::time_t t = std::chrono::system_clock::to_time_t(sys);
        char text[64];
        std::strftime(text, sizeof text, L("%Y-%m-%d %H:%M:%S", "%d/%m/%Y %H:%M:%S"), std::localtime(&t));
        s.when = text;
        listing += name + '\t' + std::to_string(s.stamp) + '\n';
        shots.push_back(std::move(s));
    }
    if (listing == g_listing) return;
    g_listing = listing;
    // Newest first; the three of one key press together, in their order.
    std::sort(shots.begin(), shots.end(), [](const Shot& a, const Shot& b) {
        if (a.second != b.second) return a.second > b.second;
        return a.kind != b.kind ? a.kind < b.kind : a.file > b.file;
    });
    std::string open_file = g_open >= 0 && g_open < int(g_shots.size()) ? g_shots[size_t(g_open)].file : "";
    g_shots = std::move(shots);
    g_open = -1;
    for (size_t i = 0; i < g_shots.size(); i++) {
        if (g_shots[i].file == open_file) g_open = int(i);
    }
    // Thumbnails of files that are gone.
    for (auto it = g_thumbs.begin(); it != g_thumbs.end();) {
        bool kept = std::any_of(g_shots.begin(), g_shots.end(), [&](const Shot& s) { return s.file == it->first; });
        if (kept) {
            ++it;
            continue;
        }
        if (it->second.texture) SDL_DestroyTexture(it->second.texture);
        it = g_thumbs.erase(it);
    }
}

// The thumbnail of a screenshot; made now when `budget` allows (it counts down).
const Thumb* thumb(SDL_Renderer* renderer, const Shot& s, int& budget) {
    auto it = g_thumbs.find(s.file);
    if (it != g_thumbs.end() && it->second.stamp == s.stamp) return &it->second;
    if (budget <= 0) return nullptr;
    budget--;
    Thumb t;
    t.stamp = s.stamp;
    if (SDL_Surface* full = load_picture(s.file)) {
        t.aspect = full->h > 0 ? float(full->w) / float(full->h) : 1;
        int h = std::min(full->h, kThumbHeight), w = std::max(1, int(std::lround(h * t.aspect)));
        SDL_Surface* small = h == full->h ? full : SDL_ScaleSurface(full, w, h, SDL_SCALEMODE_LINEAR);
        if (small) {
            t.texture = SDL_CreateTextureFromSurface(renderer, small);
            if (small != full) SDL_DestroySurface(small);
        }
        SDL_DestroySurface(full);
    } else {
        NTR_LOG("[launcher] cannot read the screenshot %s: %s\n", s.file.c_str(), SDL_GetError());
    }
    if (it != g_thumbs.end() && it->second.texture) SDL_DestroyTexture(it->second.texture);
    g_thumbs[s.file] = t;
    return &g_thumbs[s.file];
}

void close_large() {
    if (g_large) SDL_DestroyTexture(g_large);
    g_large = nullptr;
    g_large_file.clear();
}

// The picture shown large, loaded when it changes.
SDL_Texture* large(SDL_Renderer* renderer, const Shot& s) {
    if (g_large_file == s.file && g_large) return g_large;
    close_large();
    g_large_file = s.file;
    if (SDL_Surface* full = load_picture(s.file)) {
        g_large = SDL_CreateTextureFromSurface(renderer, full);
        g_large_w = full->w;
        g_large_h = full->h;
        if (g_large) SDL_SetTextureScaleMode(g_large, SDL_SCALEMODE_LINEAR);
        SDL_DestroySurface(full);
    }
    return g_large;
}

// Width / height of a screenshot: its thumbnail's once made; before that, from the name the
// game gives it (-completa: the whole 16:9 layout; -ampliada: the enlarged bottom screen, 1536
// x 1296 units of it; otherwise both screens stacked).
float shape(const Shot& s) {
    auto it = g_thumbs.find(s.file);
    if (it != g_thumbs.end() && it->second.texture) return std::clamp(it->second.aspect, 0.4f, 2.5f);
    return s.kind == 2 ? 16.0f / 9.0f : s.kind == 1 ? 1536.0f / 1296.0f : 256.0f / 384.0f;
}

ImTextureRef ref(SDL_Texture* texture) { return ImTextureRef(ImTextureID(intptr_t(texture))); }

// The large picture over the page: the picture fitted to the window, the name and date under
// it, and the previous / next ones with the arrows (or the buttons). Escape, the gamepad's B or
// a click outside the picture closes it.
void viewer(SDL_Renderer* renderer) {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.02f, 0.03f, 0.06f, 0.94f));
    ImGui::Begin("##viewer", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                          ImGuiWindowFlags_NoNav);
    ImGui::PopStyleColor();
    ImGui::SetWindowFocus();
    const Shot& s = g_shots[size_t(g_open)];
    const float em = ImGui::GetFontSize(), bar = ImGui::GetFrameHeightWithSpacing() + em * 0.5f;
    const ImVec2 avail(vp->WorkSize.x - em * 2, vp->WorkSize.y - bar - em * 2);
    bool close = ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false);
    int step = 0;
    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow) || ImGui::IsKeyPressed(ImGuiKey_GamepadDpadLeft)) step = -1;
    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow) || ImGui::IsKeyPressed(ImGuiKey_GamepadDpadRight)) step = 1;
    if (SDL_Texture* texture = large(renderer, s)) {
        float fit = std::min(avail.x / float(g_large_w), avail.y / float(g_large_h));
        if (fit > 1) fit = std::floor(fit);  // small pictures by whole pixels
        ImVec2 size(g_large_w * fit, g_large_h * fit);
        ImGui::SetCursorPos(ImVec2((vp->WorkSize.x - size.x) / 2, em + (avail.y - size.y) / 2));
        ImGui::Image(ref(texture), size);
    } else {
        ImGui::SetCursorPos(ImVec2(em, em));
        ImGui::TextDisabled("%s", L("Could not open this picture.", "Não foi possível abrir esta imagem."));
    }
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemHovered() && ImGui::GetMousePos().y < vp->WorkPos.y + vp->WorkSize.y - bar) {
        close = true;
    }
    // The bar at the bottom.
    ImGui::SetCursorPos(ImVec2(em, vp->WorkSize.y - bar));
    ImGui::BeginDisabled(g_open <= 0);
    if (ImGui::Button("<")) step = -1;
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(g_open + 1 >= int(g_shots.size()));
    if (ImGui::Button(">")) step = 1;
    ImGui::EndDisabled();
    ImGui::SameLine(0, em);
    ImGui::AlignTextToFramePadding();
    ImGui::Text("%s", s.file.c_str());
    ImGui::SameLine(0, em);
    ImGui::TextDisabled("%s  ·  %d / %d", s.when.c_str(), g_open + 1, int(g_shots.size()));
    const char* open_label = L("Open file", "Abrir arquivo");
    const char* close_label = L("Close", "Fechar");
    float right = ImGui::CalcTextSize(open_label).x + ImGui::CalcTextSize(close_label).x + ImGui::GetStyle().FramePadding.x * 4 +
                  ImGui::GetStyle().ItemSpacing.x;
    ImGui::SameLine(std::max(ImGui::GetCursorPosX(), vp->WorkSize.x - em - right));
    if (ImGui::Button(open_label)) {
        std::string url = "file:///" + paths().screenshots() + s.file;
        for (char& c : url) {
            if (c == '\\') c = '/';
        }
        SDL_OpenURL(url.c_str());
    }
    ImGui::SameLine();
    if (ImGui::Button(close_label)) close = true;
    ImGui::End();
    if (step) g_open = std::clamp(g_open + step, 0, int(g_shots.size()) - 1);
    if (close) {
        g_open = -1;
        close_large();
    }
}

}  // namespace

bool gallery_viewing() { return g_open >= 0; }

void gallery_tab(SDL_Renderer* renderer) {
    if (ImGui::GetTime() - g_listing_time > 1.5) {
        g_listing_time = ImGui::GetTime();
        scan();
    }
    ImGui::TextDisabled("%s", L("Screenshots taken in the game (F12, or the key chosen under Settings > Shortcuts).",
                                "Capturas de tela tiradas no jogo (F12, ou a tecla escolhida em Configurações > Atalhos)."));
    ImGui::SameLine();
    if (ImGui::SmallButton(L("Open the screenshots folder", "Abrir pasta de screenshots"))) open_folder(paths().screenshots());
    ImGui::Spacing();
    if (g_shots.empty()) {
        ImGui::TextDisabled("%s", L("No screenshots yet.", "Nenhuma captura ainda."));
        return;
    }
    // Tests: IE3_LAUNCHER_VIEW opens the newest screenshot large.
    static bool test_view = std::getenv("IE3_LAUNCHER_VIEW") != nullptr;
    if (test_view) {
        g_open = 0;
        test_view = false;
    }
    // The thumbnails in rows of the same height, each as wide as its picture's shape (as many as
    // fit in a row), with the date under it.
    const float em = ImGui::GetFontSize(), spacing = em * 0.8f;
    const float cell_h = em * 13, label_h = ImGui::GetTextLineHeightWithSpacing();
    ImGui::BeginChild("grid", ImVec2(0, 0), 0, g_open >= 0 ? ImGuiWindowFlags_NoNav : 0);
    const float avail = ImGui::GetContentRegionAvail().x;
    int budget = 2;
    float row_w = 0;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    for (size_t i = 0; i < g_shots.size(); i++) {
        const Shot& s = g_shots[i];
        const float cell_w = cell_h * shape(s);
        if (i > 0 && row_w + spacing + cell_w <= avail) {
            ImGui::SameLine(0, spacing);
            row_w += spacing + cell_w;
        } else {
            row_w = cell_w;
        }
        ImGui::PushID(int(i));
        ImGui::BeginGroup();
        const ImVec2 p = ImGui::GetCursorScreenPos();
        bool clicked = ImGui::InvisibleButton("##shot", ImVec2(cell_w, cell_h), ImGuiButtonFlags_EnableNav);
        bool lit = ImGui::IsItemHovered() || (ImGui::IsItemFocused() && ImGui::GetIO().NavVisible);
        if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        // Only the visible thumbnails are made.
        const Thumb* t = ImGui::IsItemVisible() ? thumb(renderer, s, budget) : nullptr;
        const ImVec2 q(p.x + cell_w, p.y + cell_h);
        dl->AddRectFilled(p, q, IM_COL32(18, 22, 34, 255), 4);
        if (t && t->texture) {
            float w = cell_w - 8, h = w / t->aspect;
            if (h > cell_h - 8) {
                h = cell_h - 8;
                w = h * t->aspect;
            }
            ImVec2 a(p.x + (cell_w - w) / 2, p.y + (cell_h - h) / 2);
            dl->AddImage(ref(t->texture), a, ImVec2(a.x + w, a.y + h));
        } else if (t) {
            dl->AddText(ImVec2(p.x + 8, p.y + 8), ImGui::GetColorU32(ImGuiCol_TextDisabled), "?");
        }
        dl->AddRect(p, q, lit ? ntr::ui::kHighlight : IM_COL32(40, 48, 70, 255), 4, 0, lit ? 2.5f : 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + cell_w);
        ImGui::TextUnformatted(s.when.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(cell_w, std::max(0.0f, label_h - ImGui::GetTextLineHeightWithSpacing())));
        ImGui::EndGroup();
        ImGui::PopID();
        if (clicked) g_open = int(i);
    }
    ImGui::EndChild();
    if (g_open >= 0) viewer(renderer);
}

}  // namespace launcher
