// The Mods tab: the mods in mods/, one folder or .zip each (format in docs/modding/MODS.md), which
// of them are on and in what order ([mods] enabled in boej.ini), adding one (a copy into
// mods/; dropping it there by hand does the same), removing one, and making one from a
// modified dump (a fan translation or ROM hack with the retail code).
//
// The same mod (the name in its mod.ini) in a new version keeps its place in the list: an
// official mod comes as mod-<name>-<version>.zip, so each version has another file name.
//
// The game checks each mod when it starts (runtime/src/mods.cpp); the tab shows the same
// check by running "boej --check-mods" in the background.
#include <SDL3/SDL.h>
#include <imgui.h>

#include <algorithm>
#include <map>
#include <sstream>
#include <thread>

#include <ntr/nitro.h>
#include <ntr/package.h>

#include "launcher.h"

namespace fs = std::filesystem;

namespace launcher {

namespace {

struct ModEntry {
    std::string folder;  // the name in mods/: a folder or "name.zip"
    bool zip = false;
    std::string name, version, author, description;
    std::string type, language;  // type = translation (tools/translate.py) and its language
    bool enabled = false;
    bool checked = false;  // the game's check ran
    size_t files = 0, overlays = 0;
    std::vector<std::string> errors, warnings;
};

// Work in the background: the check, an install or a mod made from a dump.
struct Worker {
    std::mutex mutex;
    std::atomic<bool> busy{false};
    std::thread thread;
    std::string status;  // for the player: what is happening or what happened
    bool status_error = false;
    std::map<std::string, ModEntry> checked;  // by folder, from boej --check-mods
    bool check_done = false;
    std::atomic<bool> refresh{true};  // list the folders again on the next frame
    std::vector<std::pair<std::string, std::string>> replaced;  // (old, new) by an install: done by refresh
};

Worker g_worker;
std::vector<ModEntry> g_mods;  // in load order: the enabled ones first
int g_selected = -1;
int g_remove = -1;  // the mod whose "Remover" was clicked: the confirmation is open
std::string g_pending_zip, g_pending_dump;  // from a file dialog or a drop (a .zip or a folder)
std::string g_listing;  // names in mods/ at the last refresh, to notice mods added by hand
double g_listing_time = 0;
std::mutex g_pending_mutex;

std::vector<std::string> enabled_list() {
    std::vector<std::string> out;
    std::stringstream list(settings().get("mods", "enabled"));
    for (std::string item; std::getline(list, item, ',');) {
        size_t a = item.find_first_not_of(" \t"), b = item.find_last_not_of(" \t");
        if (a != std::string::npos) item = item.substr(a, b - a + 1);
        else continue;
        if (std::find(out.begin(), out.end(), item) == out.end()) out.push_back(item);
    }
    return out;
}

void save_enabled() {
    std::string list;
    for (const ModEntry& m : g_mods) {
        if (!m.enabled) continue;
        if (!list.empty()) list += ", ";
        list += m.folder;
    }
    settings_set("mods", "enabled", list);
}

void set_status(const std::string& text, bool error = false) {
    std::lock_guard lock(g_worker.mutex);
    g_worker.status = text;
    g_worker.status_error = error;
}

// Folder names become entries of a comma-separated list and file names on every system.
std::string folder_name(std::string s) {
    for (char& c : s) {
        if (c == ',' || c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' ||
            c == '|')
            c = '-';
    }
    // "5421 - Nome (J)(Grupo)": the number of a release list says nothing.
    size_t digits = s.find_first_not_of("0123456789");
    if (digits > 0 && digits != std::string::npos && s.compare(digits, 3, " - ") == 0) s = s.substr(digits + 3);
    if (s.size() > 96) {
        size_t cut = 96;
        while (cut > 0 && (uint8_t(s[cut]) & 0xC0) == 0x80) cut--;  // not inside a UTF-8 character
        s.resize(cut);
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '.')) s.pop_back();
    return s.empty() ? "mod" : s;
}

void join_worker() {
    if (g_worker.thread.joinable() && !g_worker.busy) g_worker.thread.join();
}

// `then_refresh`: the work changed the folders, so list them (and check) again.
void start(std::function<void()> work, bool then_refresh = true) {
    join_worker();
    if (g_worker.busy) return;
    g_worker.busy = true;
    g_worker.thread = std::thread([work = std::move(work), then_refresh] {
        work();
        if (then_refresh) g_worker.refresh = true;
        g_worker.busy = false;
    });
}

// --- the check ------------------------------------------------------------------------

void run_check() {
    std::error_code ec;
    if (!fs::exists(ntr::fs_path(paths().game()), ec)) return;
    std::string out;
    run_process({paths().game(), "--check-mods"}, out, paths().root);
    std::map<std::string, ModEntry> result;
    std::istringstream lines(out);
    for (std::string line; std::getline(lines, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::vector<std::string> f;
        std::stringstream fields(line);
        for (std::string field; std::getline(fields, field, '\t');) f.push_back(field);
        if (f.size() >= 7 && f[0] == "mod") {
            ModEntry& m = result[f[1]];
            m.checked = true;
            m.files = std::strtoul(f[3].c_str(), nullptr, 10);
            m.overlays = std::strtoul(f[4].c_str(), nullptr, 10);
        } else if (f.size() >= 3 && f[0] == "error") {
            result[f[1]].errors.push_back(f[2]);
        } else if (f.size() >= 3 && f[0] == "warning") {
            result[f[1]].warnings.push_back(f[2]);
        }
    }
    std::lock_guard lock(g_worker.mutex);
    g_worker.checked = std::move(result);
    g_worker.check_done = true;
}

bool is_zip_name(const std::string& name) {
    std::string ext = ntr::utf8_string(ntr::fs_path(name).extension());
    for (char& c : ext) c = char(std::tolower(uint8_t(c)));
    return ext == ".zip";
}

// The mods in mods/: folders and .zip files (not the ones starting with a dot).
std::vector<std::string> mod_names() {
    std::vector<std::string> names;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(ntr::fs_path(paths().mods()), ec)) {
        std::string name = ntr::utf8_string(entry.path().filename());
        if (name.empty() || name[0] == '.') continue;
        if (entry.is_directory(ec) || (entry.is_regular_file(ec) && is_zip_name(name))) names.push_back(name);
    }
    std::sort(names.begin(), names.end());
    return names;
}

std::string listing(const std::vector<std::string>& names) {
    std::string out;
    for (const std::string& n : names) out += n + "\n";
    return out;
}

// What mod.ini says of a mod in mods/ (the name is the folder's when it has none).
ModEntry read_mod(const std::string& folder) {
    ModEntry m;
    m.folder = folder;
    m.zip = is_zip_name(folder);
    ntr::ModPackage package;
    std::string error;
    std::vector<uint8_t> text;
    ntr::Config ini;
    if (package.open(paths().mods() + folder, error)) {
        if (const ntr::ModPackage::Entry* e = package.find("mod.ini"); e && package.read(*e, text, error))
            ntr::ini_parse(std::string(text.begin(), text.end()), folder + "/mod.ini", ini);
    }
    m.name = ini.get("mod", "name");
    m.version = ini.get("mod", "version");
    m.author = ini.get("mod", "author");
    m.description = ini.get("mod", "description");
    m.type = ini.get("mod", "type");
    m.language = ini.get("mod", "language");
    if (m.name.empty()) m.name = folder;
    return m;
}

// "1.10" is newer than "1.9": the numbers between dots compare as numbers.
bool newer_version(const std::string& a, const std::string& b) {
    size_t i = 0, j = 0;
    while (i < a.size() || j < b.size()) {
        size_t ei = a.find('.', i), ej = b.find('.', j);
        if (ei == std::string::npos) ei = a.size();
        if (ej == std::string::npos) ej = b.size();
        std::string x = i < a.size() ? a.substr(i, ei - i) : "0", y = j < b.size() ? b.substr(j, ej - j) : "0";
        auto number = [](const std::string& t) {
            return !t.empty() && t.size() <= 18 && t.find_first_not_of("0123456789") == std::string::npos;
        };
        bool numbers = number(x) && number(y);
        if (numbers ? std::stoull(x) != std::stoull(y) : x != y)
            return numbers ? std::stoull(x) > std::stoull(y) : x > y;
        i = ei + 1;
        j = ej + 1;
    }
    return false;
}

// The enabled list after an update: a mod the launcher replaced (`replaced`, whose old copy
// is then deleted) or one with a newer version put in mods/ by hand takes the place of the
// old one, which is left off.
std::vector<std::string> update_enabled(std::vector<std::string> enabled,
                                        const std::vector<std::pair<std::string, std::string>>& replaced) {
    std::error_code ec;
    for (const auto& [old, now] : replaced) {
        auto it = std::find(enabled.begin(), enabled.end(), old);
        if (it != enabled.end()) {
            if (std::find(enabled.begin(), enabled.end(), now) == enabled.end()) *it = now;
            else enabled.erase(it);
        }
        fs::remove_all(ntr::fs_path(paths().mods() + old), ec);
    }
    std::vector<ModEntry> all;
    for (const std::string& folder : mod_names()) all.push_back(read_mod(folder));
    for (std::string& on : enabled) {
        auto mine = std::find_if(all.begin(), all.end(), [&](const ModEntry& m) { return m.folder == on; });
        if (mine == all.end()) continue;
        const ModEntry* best = &*mine;
        for (const ModEntry& m : all) {
            if (m.name == best->name && m.folder != best->folder && newer_version(m.version, best->version) &&
                std::find(enabled.begin(), enabled.end(), m.folder) == enabled.end())
                best = &m;
        }
        on = best->folder;
    }
    return enabled;
}

// Lists mods/ again and starts the check.
void refresh() {
    std::vector<std::pair<std::string, std::string>> replaced;
    {
        std::lock_guard lock(g_worker.mutex);
        replaced.swap(g_worker.replaced);
    }
    std::vector<std::string> before = enabled_list(), enabled = update_enabled(before, replaced);
    std::vector<ModEntry> mods;
    std::vector<std::string> folders = mod_names();
    g_listing = listing(folders);
    auto add = [&](const std::string& folder, bool on) {
        ModEntry m = read_mod(folder);
        m.enabled = on;
        mods.push_back(std::move(m));
    };
    for (const std::string& folder : enabled) {
        if (std::find(folders.begin(), folders.end(), folder) != folders.end()) add(folder, true);
    }
    for (const std::string& folder : folders) {
        if (std::find(enabled.begin(), enabled.end(), folder) == enabled.end()) add(folder, false);
    }
    std::string selected = g_selected >= 0 && g_selected < int(g_mods.size()) ? g_mods[g_selected].folder : "";
    g_mods = std::move(mods);
    g_selected = -1;
    if (const char* env = std::getenv("IE3_LAUNCHER_MOD"); env && selected.empty()) selected = env;  // tests
    for (size_t i = 0; i < g_mods.size(); i++) {
        if (g_mods[i].folder == selected) g_selected = int(i);
    }
    // An enabled folder that is gone leaves the list; an updated mod takes its old place.
    if (enabled != before ||
        enabled.size() != size_t(std::count_if(g_mods.begin(), g_mods.end(), [](const ModEntry& m) { return m.enabled; })))
        save_enabled();
    {
        std::lock_guard lock(g_worker.mutex);
        g_worker.check_done = false;
    }
    start(run_check, false);
}

// --- installing and making mods ---------------------------------------------------------

// Copies a .zip or a folder into mods/ (what a player can also do by hand). It must be a
// mod: mod.ini at its top (or, in a zip, in its only folder).
void install(const std::string& source) {
    fs::path from = ntr::fs_path(source);
    std::string name = ntr::utf8_string(from.filename());
    set_status(L("Adding ", "Adicionando ") + name + "...");
    std::error_code ec;
    bool folder = fs::is_directory(from, ec);
    if (!folder && !is_zip_name(name)) {
        set_status(L("A mod is a folder or a .zip.", "Um mod é uma pasta ou um .zip."), true);
        return;
    }
    ntr::ModPackage package;
    std::string error;
    if (!package.open(source, error)) {
        set_status(name + ": " + error, true);
        return;
    }
    if (!package.find("mod.ini")) {
        set_status(name + L(" is not a mod: it has no mod.ini (see docs/modding/MODS.md).", " não é um mod: falta o mod.ini (veja docs/modding/MODS.pt-BR.md)."), true);
        return;
    }
    if (folder) {
        name = folder_name(name);
    } else {
        name = folder_name(ntr::utf8_string(from.stem())) + ".zip";
    }
    fs::path mods = ntr::fs_path(paths().mods()), target = mods / ntr::fs_path(name), tmp = mods / ".copiando";
    if (fs::equivalent(from, target, ec)) {
        set_status(name + L(" is already in the mods folder.", " já está na pasta mods."));
        return;
    }
    fs::create_directories(mods, ec);
    fs::remove_all(tmp, ec);
    ec.clear();
    if (folder) {
        fs::copy(from, tmp, fs::copy_options::recursive, ec);
    } else {
        fs::copy_file(from, tmp, ec);
    }
    bool replaced = fs::exists(target);
    if (!ec) {
        fs::remove_all(target, ec);
        fs::rename(tmp, target, ec);
    }
    if (ec) {
        fs::remove_all(tmp, ec);
        set_status(L("Could not copy the mod to the mods folder: ", "Não foi possível copiar o mod para a pasta mods: ") + ec.message(), true);
        return;
    }
    // Other copies of the same mod (another version, another file name) give it their place.
    ModEntry added = read_mod(name);
    std::vector<std::string> older;
    for (const std::string& folder : mod_names()) {
        if (folder == name) continue;
        ModEntry m = read_mod(folder);
        if (m.name == added.name && !newer_version(m.version, added.version)) older.push_back(folder);
    }
    {
        std::lock_guard lock(g_worker.mutex);
        for (const std::string& folder : older) g_worker.replaced.emplace_back(folder, name);
    }
    if (!older.empty() || replaced) {
        std::string from;
        for (const std::string& folder : older) from += (from.empty() ? "" : ", ") + folder;
        set_status(L("Mod updated: ", "Mod atualizado: ") + name +
                   (added.version.empty() ? "" : L(" (version ", " (versão ") + added.version + ")") +
                   (from.empty() ? "" : "; " + from + L(" was deleted from the mods folder and the new one takes its place",
                                                         " foi apagado da pasta mods e o novo fica no lugar dele")) +
                   L(". The original was not changed.", ". O original não foi alterado."));
        return;
    }
    set_status(L("Mod added: ", "Mod adicionado: ") + name +
               L(" (copied to the mods folder; the original was not changed). Turn it on in the list.",
                 " (copiado para a pasta mods; o original não foi alterado). Ligue-o na lista."));
}

bool write_file(const fs::path& path, const std::vector<uint8_t>& data) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    FILE* f = ntr::open_file(path, "wb");
    if (!f) return false;
    bool ok = std::fwrite(data.data(), 1, data.size(), f) == data.size();
    return std::fclose(f) == 0 && ok;
}

// The files and overlays where a modified dump differs from the retail game (boej.pak).
// The game's check (shown in the list) then tells whether only data changed.
void mod_from_dump(const std::string& dump) {
    std::string stem = ntr::utf8_string(ntr::fs_path(dump).stem());
    set_status(L("Comparing ", "Comparando ") + stem + L(" with the original game...", " com o jogo original..."));
    ntr::RomImage retail, other;
    if (!retail.open(paths().pak())) {
        set_status(L("boej.pak is missing: prepare the game first (home screen).", "Falta o boej.pak: prepare o jogo antes (tela inicial)."), true);
        return;
    }
    if (!other.open(dump) || other.is_pak()) {
        set_status(L("Could not read ", "Não foi possível ler ") + dump + ".", true);
        return;
    }
    if (retail.read32(0x0C) != other.read32(0x0C)) {
        set_status(L("That dump is of another game (the code is not BOEJ).", "Esse dump é de outro jogo (o código não é BOEJ)."), true);
        return;
    }
    uint32_t arm9_size = retail.read32(0x2C);
    if (other.read32(0x2C) != arm9_size || retail.bytes(retail.read32(0x20), arm9_size) != other.bytes(other.read32(0x20), arm9_size)) {
        set_status(L("That dump's main code (ARM9) differs from the original; mods can only change data.",
                     "O código principal (ARM9) desse dump é diferente do original; mods só podem mudar dados."),
                   true);
        return;
    }
    std::vector<ntr::NitroFile> base = ntr::nitro_files(retail), mine = ntr::nitro_files(other);
    std::map<std::string, const ntr::NitroFile*> by_path;
    for (const ntr::NitroFile& f : base) by_path[f.path] = &f;
    std::error_code ec;
    fs::path mods = ntr::fs_path(paths().mods());
    std::string folder = folder_name(stem);
    fs::path target = mods / ntr::fs_path(folder), tmp = mods / ".criando";
    fs::remove_all(tmp, ec);
    size_t files = 0, overlays = 0;
    for (const ntr::NitroFile& f : mine) {
        auto it = by_path.find(f.path);
        if (it == by_path.end() || f.end < f.start) continue;
        std::vector<uint8_t> data = other.bytes(f.start, f.end - f.start);
        if (data == retail.bytes(it->second->start, it->second->end - it->second->start)) continue;
        if (!write_file(tmp / "files" / ntr::fs_path(f.path), data)) {
            set_status(L("Could not write to the mods/ folder.", "Não foi possível gravar na pasta mods/."), true);
            return;
        }
        files++;
    }
    std::vector<ntr::NitroOverlay> ovb = ntr::nitro_overlays(retail), ovm = ntr::nitro_overlays(other);
    for (size_t i = 0; i < ovb.size() && i < ovm.size(); i++) {
        if (ovb[i].id == 129) continue;  // copy protection: encrypted, never part of a mod
        std::vector<uint8_t> a = ntr::nitro_overlay_image(retail, ovb[i]), b = ntr::nitro_overlay_image(other, ovm[i]);
        if (a.empty() || b.empty() || a == b) continue;
        if (b.size() < a.size()) b.insert(b.end(), a.begin() + b.size(), a.end());  // the slot keeps its size
        char name[16];
        std::snprintf(name, sizeof name, "ov%03u.bin", ovb[i].id);
        if (!write_file(tmp / "overlays" / name, b)) {
            set_status(L("Could not write to the mods/ folder.", "Não foi possível gravar na pasta mods/."), true);
            return;
        }
        overlays++;
    }
    if (files == 0 && overlays == 0) {
        fs::remove_all(tmp, ec);
        set_status(L("That dump does not change any of the game's files.", "Esse dump não muda nenhum arquivo do jogo."), true);
        return;
    }
    std::string ini = L("; Made by the launcher from ", "; Feito pelo launcher a partir de ") +
                      ntr::utf8_string(ntr::fs_path(dump).filename()) + ".\n[mod]\nname = " + folder +
                      "\nversion = 1.0\nauthor =\ndescription = " + std::to_string(files) + L(" files and ", " arquivos e ") +
                      std::to_string(overlays) + L(" overlays of the modified dump", " overlays do dump modificado") +
                      "\ngame = BOEJ\n";
    write_file(tmp / "mod.ini", std::vector<uint8_t>(ini.begin(), ini.end()));
    fs::remove_all(target, ec);
    fs::rename(tmp, target, ec);
    if (ec) {
        set_status(L("Could not create the mod's folder: ", "Não foi possível criar a pasta do mod: ") + ec.message(), true);
        return;
    }
    set_status(L("Mod created: ", "Mod criado: ") + folder + " (" + std::to_string(files) + L(" files, ", " arquivos, ") +
               std::to_string(overlays) + L(" overlays). Turn it on in the list.", " overlays). Ligue-o na lista."));
}

void choose(SDL_Window* window, bool zip) {
    static const SDL_DialogFileFilter kZip[] = {{"Mod (*.zip)", "zip"}};
    static SDL_DialogFileFilter kNds[1];  // static: the dialog may read it after this returns
    kNds[0] = {L("Modified dump (*.nds)", "Dump modificado (*.nds)"), "nds"};
    SDL_ShowOpenFileDialog(
        [](void* is_zip, const char* const* files, int) {
            if (!files || !files[0]) return;
            std::lock_guard lock(g_pending_mutex);
            (is_zip ? g_pending_zip : g_pending_dump) = files[0];
        },
        zip ? &g_worker : nullptr, window, zip ? kZip : kNds, 1, nullptr, false);
}

// --- the tab ----------------------------------------------------------------------------

std::string warning_count(size_t n) {
    return std::to_string(n) + (n == 1 ? L(" warning", " aviso") : L(" warnings", " avisos"));
}

void details(ModEntry& m) {
    ImGui::TextUnformatted(m.name.c_str());
    if (!m.version.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled(L("version %s", "versão %s"), m.version.c_str());
    }
    if (!m.author.empty()) ImGui::TextDisabled(L("by %s", "por %s"), m.author.c_str());
    if (m.type == "translation")
        ImGui::TextDisabled(L("Translation (%s)", "Tradução (%s)"), m.language.empty() ? "?" : m.language.c_str());
    if (!m.description.empty()) ImGui::TextWrapped("%s", m.description.c_str());
    ImGui::TextDisabled("%s: mods/%s", m.zip ? L("File", "Arquivo") : L("Folder", "Pasta"), m.folder.c_str());
    if (m.checked) ImGui::TextDisabled(L("Replaces %zu files and %zu overlays.", "Troca %zu arquivos e %zu overlays."), m.files, m.overlays);
    for (const std::string& e : m.errors) ImGui::TextColored(ImVec4(1, 0.45f, 0.4f, 1), L("Error: %s", "Erro: %s"), e.c_str());
    if (!m.warnings.empty() && ImGui::TreeNode("warnings", "%s", warning_count(m.warnings.size()).c_str())) {
        for (const std::string& w : m.warnings) ImGui::TextColored(ImVec4(0.95f, 0.8f, 0.4f, 1), "%s", w.c_str());
        ImGui::TreePop();
    }
    ImGui::Spacing();
    if (ImGui::Button(m.zip ? L("Show in the mods folder", "Mostrar na pasta mods") : L("Open the mod's folder", "Abrir a pasta do mod")))
        open_folder(m.zip ? paths().mods() : paths().mods() + m.folder);
    ImGui::SameLine();
    if (ImGui::Button(L("Remove...", "Remover..."))) g_remove = g_selected;
}

// The confirmation of "Remover" (from the list or from the details). Only the copy in mods/
// goes: a mod added through the launcher was copied there, and its source stays as it was.
void remove_dialog() {
    if (g_remove >= int(g_mods.size())) g_remove = -1;
    if (g_remove >= 0 && !ImGui::IsPopupOpen("###remove")) ImGui::OpenPopup("###remove");
    if (!ImGui::BeginPopupModal(L("Remove mod###remove", "Remover mod###remove"), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        g_remove = -1;
        return;
    }
    ModEntry& m = g_mods[std::max(g_remove, 0)];
    ImGui::Text(L("Delete the mod \"%s\"?", "Apagar o mod \"%s\"?"), m.name.c_str());
    ImGui::TextDisabled("%s: mods/%s", m.zip ? L("File", "Arquivo") : L("Folder", "Pasta"), m.folder.c_str());
    ImGui::Spacing();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 34);
    ImGui::TextWrapped("%s", L("Only the copy in the game's mods folder is deleted. If you added the mod through the launcher, "
                               "the original file or folder it was copied from stays where it was, unchanged.",
                               "Só a cópia que está na pasta mods do jogo é apagada. Se você adicionou o mod pelo launcher, "
                               "o arquivo ou a pasta original, de onde ele foi copiado, continua onde estava, sem mudanças."));
    ImGui::PopTextWrapPos();
    ImGui::TextDisabled("%s", L("Saves do not change.", "Os saves não mudam."));
    ImGui::Spacing();
    bool close = false;
    if (ImGui::Button(L("Delete", "Apagar"))) {
        std::error_code ec;
        fs::remove_all(ntr::fs_path(paths().mods() + m.folder), ec);
        m.enabled = false;
        save_enabled();
        set_status(ec ? L("Could not delete: ", "Não foi possível apagar: ") + ec.message()
                      : L("Mod removed: ", "Mod removido: ") + m.folder +
                            L(" (only the copy in the mods folder; the original was not touched).",
                              " (só a cópia da pasta mods; o original não foi mexido)."),
                   bool(ec));
        g_worker.refresh = true;
        close = true;
    }
    ImGui::SameLine();
    if (ImGui::Button(L("Cancel", "Cancelar")) || ImGui::IsKeyPressed(ImGuiKey_Escape)) close = true;
    if (close) {
        g_remove = -1;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

}  // namespace

// ie3-launcher --install-mod <zip> / --mod-from-dump <nds>: the same as the buttons.
int mods_cli(const std::string& command, const std::string& path) {
    if (command == "--install-mod") {
        install(path);
        std::vector<std::pair<std::string, std::string>> replaced;
        {
            std::lock_guard lock(g_worker.mutex);
            replaced.swap(g_worker.replaced);
        }
        std::vector<std::string> before = enabled_list(), enabled = update_enabled(before, replaced);
        if (enabled != before) {
            std::string list;
            for (const std::string& folder : enabled) list += (list.empty() ? "" : ", ") + folder;
            settings_set("mods", "enabled", list);
        }
    } else {
        mod_from_dump(path);
    }
    std::lock_guard lock(g_worker.mutex);
    NTR_LOG("%s%s\n", g_worker.status_error ? "error: " : "", g_worker.status.c_str());
    return g_worker.status_error ? 1 : 0;
}

void mods_drop(const std::string& path) {
    std::lock_guard lock(g_pending_mutex);
    g_pending_zip = path;
}

void mods_tab(SDL_Window* window) {
    join_worker();
    {
        std::lock_guard lock(g_pending_mutex);
        if (!g_worker.busy && !g_pending_zip.empty()) {
            start([zip = g_pending_zip] { install(zip); });
            g_pending_zip.clear();
        } else if (!g_worker.busy && !g_pending_dump.empty()) {
            start([dump = g_pending_dump] { mod_from_dump(dump); });
            g_pending_dump.clear();
        }
    }
    bool busy = g_worker.busy;
    // Mods put in (or taken out of) the folder by hand show up without "Atualizar".
    if (!busy && ImGui::GetTime() - g_listing_time > 1.5) {
        g_listing_time = ImGui::GetTime();
        if (listing(mod_names()) != g_listing) g_worker.refresh = true;
    }
    if (!busy && g_worker.refresh.exchange(false)) {
        refresh();
        busy = true;
    }
    {
        std::lock_guard lock(g_worker.mutex);
        if (g_worker.check_done) {
            for (ModEntry& m : g_mods) {
                auto it = g_worker.checked.find(m.folder);
                if (it == g_worker.checked.end()) continue;
                m.checked = it->second.checked;
                m.files = it->second.files;
                m.overlays = it->second.overlays;
                m.errors = it->second.errors;
                m.warnings = it->second.warnings;
            }
            g_worker.check_done = false;
        }
    }

    ImGui::TextWrapped("%s", L("Each mod is a folder or a .zip in the mods folder, next to the game: put it there (or use "
                               "\"Add\", or drag it onto this window) and turn it on in the list. When two mods change the "
                               "same file, the one further down wins. Changes apply the next time the game starts.",
                               "Cada mod é uma pasta ou um .zip dentro da pasta mods, ao lado do jogo: coloque-o lá (ou use "
                               "\"Adicionar\", ou arraste para esta janela) e ligue-o na lista. Quando dois mods mudam o mesmo "
                               "arquivo, vale o que está mais abaixo. As mudanças valem na próxima vez que o jogo abrir."));
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", L("Add only copies the mod to the game's mods folder, and Remove only deletes that copy: the "
                               "original file or folder is never changed.",
                               "Adicionar só copia o mod para a pasta mods do jogo, e Remover só apaga essa cópia: o arquivo ou "
                               "a pasta original nunca é alterado."));
    ImGui::PopStyleColor();
    ImGui::Spacing();
    if (ImGui::Button(L("Add mod (.zip)...", "Adicionar mod (.zip)..."))) choose(window, true);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", L("Copies the .zip to the game's mods folder. The chosen file is not changed or moved.",
                                  "Copia o .zip para a pasta mods do jogo. O arquivo escolhido não é alterado nem movido."));
    }
    ImGui::SameLine();
    if (ImGui::Button(L("Create from a modified dump...", "Criar de um dump modificado..."))) choose(window, false);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", L("A translation or hack as an .nds becomes a mod with the files it changes.\n"
                                  "It only works if the dump's code is the original game's.",
                                  "Uma tradução ou um hack em .nds vira um mod com os arquivos que ele muda.\n"
                                  "Só funciona se o código do dump for o do jogo original."));
    }
    ImGui::SameLine();
    if (ImGui::Button(L("Open the mods folder", "Abrir a pasta mods"))) open_folder(paths().mods());
    ImGui::SameLine();
    ImGui::BeginDisabled(busy);
    if (ImGui::Button(L("Refresh", "Atualizar"))) g_worker.refresh = true;
    ImGui::EndDisabled();
    {
        std::lock_guard lock(g_worker.mutex);
        if (!g_worker.status.empty()) {
            ImVec4 color = g_worker.status_error ? ImVec4(1, 0.45f, 0.4f, 1) : ImVec4(0.6f, 0.85f, 0.6f, 1);
            ImGui::PushStyleColor(ImGuiCol_Text, color);
            ImGui::TextWrapped("%s", g_worker.status.c_str());
            ImGui::PopStyleColor();
        }
    }
    ImGui::Spacing();

    float list_height = std::max(ImGui::GetContentRegionAvail().y * 0.5f, 120.0f);
    bool changed = false;
    if (ImGui::BeginTable("mods", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
                          ImVec2(0, list_height))) {
        ImGui::TableSetupColumn(L("On", "Ligado"), ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Mod", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn(L("Status", "Estado"), ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();
        int move_from = -1, move_to = -1;
        for (int i = 0; i < int(g_mods.size()); i++) {
            ModEntry& m = g_mods[i];
            ImGui::PushID(i);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (ImGui::Checkbox("##on", &m.enabled)) changed = true;
            ImGui::TableNextColumn();
            if (ImGui::Selectable(m.name.c_str(), g_selected == i, ImGuiSelectableFlags_AllowOverlap)) g_selected = i;
            if (!m.version.empty()) {
                ImGui::SameLine();
                ImGui::TextDisabled("v%s", m.version.c_str());
            }
            if (m.type == "translation" && !m.language.empty()) {
                ImGui::SameLine();
                ImGui::TextDisabled("%s", m.language.c_str());
            }
            if (m.name != m.folder) {
                ImGui::SameLine();
                ImGui::TextDisabled("%s", m.folder.c_str());  // two mods can have the same name
            }
            ImGui::TableNextColumn();
            if (!m.errors.empty()) ImGui::TextColored(ImVec4(1, 0.45f, 0.4f, 1), "%s", L("error", "com erro"));
            else if (!m.checked) ImGui::TextDisabled("%s", busy ? L("checking...", "conferindo...") : "-");
            else if (!m.warnings.empty()) ImGui::TextColored(ImVec4(0.95f, 0.8f, 0.4f, 1), "%s", warning_count(m.warnings.size()).c_str());
            else ImGui::TextColored(ImVec4(0.6f, 0.85f, 0.6f, 1), "ok");
            ImGui::SameLine();
            ImGui::BeginDisabled(i == 0);
            if (ImGui::ArrowButton("##up", ImGuiDir_Up)) move_from = i, move_to = i - 1;
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(i + 1 == int(g_mods.size()));
            if (ImGui::ArrowButton("##down", ImGuiDir_Down)) move_from = i, move_to = i + 1;
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button(L("Remove", "Remover"))) g_remove = i;
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(L("Deletes mods/%s (asks first). The original is not touched.",
                                    "Apaga mods/%s (pede confirmação). O original não é mexido."), m.folder.c_str());
            }
            ImGui::PopID();
        }
        if (g_mods.empty()) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", L("No mods in the mods folder.", "Nenhum mod na pasta mods."));
        }
        if (move_from >= 0) {
            std::swap(g_mods[move_from], g_mods[move_to]);
            if (g_selected == move_from) g_selected = move_to;
            else if (g_selected == move_to) g_selected = move_from;
            changed = true;
        }
        ImGui::EndTable();
    }
    if (changed) {
        save_enabled();
        g_worker.refresh = true;  // conflicts depend on what is on and in what order
    }
    ImGui::Spacing();
    if (g_selected >= 0 && g_selected < int(g_mods.size())) {
        details(g_mods[g_selected]);
    } else {
        ImGui::TextDisabled("%s", L("Pick a mod in the list to see its details. To make your own, see docs/modding/MODS.md.",
                                    "Escolha um mod na lista para ver os detalhes. Para fazer o seu, veja docs/modding/MODS.pt-BR.md."));
    }
    remove_dialog();
}

}  // namespace launcher
