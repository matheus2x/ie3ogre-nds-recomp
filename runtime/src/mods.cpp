// Mods: one folder or .zip per mod in mods/ next to the executable, turned on in boej.ini
// ([mods] enabled = name, name.zip; a later one wins over an earlier one). Inside each
// (ntr/package.h finds the root of a zip), as described in docs/MODS.md:
//
//   mod.ini              name, version, author, description, game = BOEJ
//   files/<path>         replaces the NitroFS file at <path> (data_iz/...)
//   overlays/ovNNN.bin   overlay NNN as it is in RAM (decompressed); only bytes outside the
//                        recompiled code may change
//
// Nothing is copied into boej.pak: the mods' files go at ROM offsets past the end of the
// image and patched copies of the FAT and of the overlay table point the game at them
// (rom_override). The game reads them like any other file, so a mod may change sizes.
//
// The code of an overlay is recompiled, so changing it in a mod would do nothing (or, for
// the literal pools baked into the generated code, half of something). Each overlay of a
// mod is compared with the retail one over the ranges the recompiler used
// (ModuleEntry::code); a difference there rejects the mod.
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <system_error>
#include <thread>

#include <SDL3/SDL_stdinc.h>
#include <ntr/nitro.h>
#include <ntr/package.h>
#include <ntr/registry.h>
#include <ntr/runtime.h>

#include "runtime.h"

namespace ntr {

namespace fs = std::filesystem;

namespace {

constexpr uint32_t kAlign = 0x200;
// DS Protect: encrypted in the ROM and decrypted by the game itself, so the retail bytes
// cannot be compared (the recompiled code comes from the decrypted ones).
constexpr unsigned kProtectedOverlay = 129;

struct ModFile {
    std::string path;  // as in NitroFS
    uint16_t id;
    uint32_t size;
    const uint8_t* data;  // in place (folder or stored zip entry), or null: inflated when read
    const ModPackage::Entry* entry;
};

struct ModOverlay {
    size_t index;  // in nitro_overlays()
    uint32_t id;
    std::vector<uint8_t> image;
    uint32_t ram_size;
};

struct Mod {
    std::string folder;  // the name in mods/: a folder or "name.zip"
    std::shared_ptr<ModPackage> package;
    std::vector<ModFile> files;
    std::vector<ModOverlay> overlays;
    std::vector<std::string> errors, warnings;
    uint64_t fingerprint = 0;
};

uint64_t g_fingerprint = 0;
std::deque<std::vector<uint8_t>> g_owned;  // overlay images and table copies the ROM reads from

uint64_t fnv(uint64_t h, const void* p, size_t n) {
    for (size_t i = 0; i < n; i++) h = (h ^ static_cast<const uint8_t*>(p)[i]) * 0x100000001B3ull;
    return h;
}

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(uint8_t(c)));
    return s;
}

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t"), b = s.find_last_not_of(" \t");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

std::string hex(uint32_t v) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "0x%08X", v);
    return buf;
}

std::string mods_dir() { return host_base_path() + "mods/"; }

std::vector<std::string> split_list(const std::string& list) {
    std::vector<std::string> out;
    size_t at = 0;
    while (at <= list.size()) {
        size_t comma = list.find(',', at);
        if (comma == std::string::npos) comma = list.size();
        std::string item = trim(list.substr(at, comma - at));
        if (!item.empty() && std::find(out.begin(), out.end(), item) == out.end()) out.push_back(item);
        at = comma + 1;
    }
    return out;
}

const ModuleEntry* module_named(const std::string& name) {
    for (uint32_t i = 0; i < kRecompModuleCount; i++) {
        if (name == kRecompModules[i].name) return &kRecompModules[i];
    }
    return nullptr;
}

bool is_zip_name(const std::string& name) {
    return name.size() > 4 && lower(name.substr(name.size() - 4)) == ".zip";
}

// Reads the mod; problems go into its errors and warnings. With `verify`, every zipped file
// is inflated and checked (boej --check-mods); otherwise that waits for the game to read it.
Mod scan_mod(const std::string& folder, const std::map<std::string, const NitroFile*>& by_path,
             const std::vector<NitroOverlay>& overlays, bool verify) {
    Mod mod;
    mod.folder = folder;
    mod.package = std::make_shared<ModPackage>();
    std::error_code ec;
    fs::path where = fs_path(mods_dir() + folder);
    if (!fs::is_directory(where, ec) && !(is_zip_name(folder) && fs::is_regular_file(where, ec))) {
        mod.errors.push_back("não existe em mods/ (uma pasta ou um .zip)");
        return mod;
    }
    if (folder.find(',') != std::string::npos) mod.errors.push_back("o nome não pode ter vírgula");
    std::string error;
    if (!mod.package->open(mods_dir() + folder, error)) {
        mod.errors.push_back(error);
        return mod;
    }
    const ModPackage& pkg = *mod.package;
    std::vector<uint8_t> bytes;
    Config ini;
    if (const ModPackage::Entry* e = pkg.find("mod.ini"); !e || !pkg.read(*e, bytes, error)) {
        mod.errors.push_back("falta o mod.ini");
    } else if (ini_parse(std::string(bytes.begin(), bytes.end()), folder + "/mod.ini", ini),
               ini.get("mod", "game") != "" && ini.get("mod", "game") != "BOEJ") {
        mod.errors.push_back("o mod é para outro jogo (" + ini.get("mod", "game") + ")");
    }
    uint64_t h = fnv(0xCBF29CE484222325ull, folder.data(), folder.size());

    for (const ModPackage::Entry& e : pkg.entries()) {
        std::string lpath = lower(e.path);
        if (lpath.rfind("files/", 0) == 0) {
            std::string rel = e.path.substr(6);
            auto found = by_path.find(lower(rel));
            if (found == by_path.end()) {
                mod.warnings.push_back(e.path + " não existe no jogo (ignorado)");
                continue;
            }
            if (e.size == 0 || e.size > 0x10000000) {
                mod.errors.push_back(e.path + (e.size ? " é maior que 256 MiB" : " está vazio"));
                continue;
            }
            if (verify && pkg.is_zip() && !pkg.read(e, bytes, error)) {
                mod.errors.push_back(error);
                continue;
            }
            const uint8_t* data = pkg.direct(e);
            if (!data && !pkg.is_zip()) {
                mod.errors.push_back("não foi possível ler " + e.path);
                continue;
            }
            mod.files.push_back({found->second->path, found->second->id, uint32_t(e.size), data, &e});
            h = fnv(h, e.path.data(), e.path.size());
            h = fnv(h, &e.size, sizeof e.size);
            h = fnv(h, &e.stamp, sizeof e.stamp);
        } else if (lpath.rfind("overlays/", 0) == 0) {
            std::string name = e.path.substr(9);
            unsigned id = 0;
            char expect[32] = {};
            if (std::sscanf(lower(name).c_str(), "ov%u", &id) == 1) std::snprintf(expect, sizeof expect, "ov%03u.bin", id);
            if (lower(name) != expect) {
                mod.warnings.push_back(e.path + ": esperado overlays/ovNNN.bin (ignorado)");
                continue;
            }
            auto ov = std::find_if(overlays.begin(), overlays.end(), [&](const NitroOverlay& o) { return o.id == id; });
            char module[8];
            std::snprintf(module, sizeof module, "ov%03u", id);
            const ModuleEntry* code = module_named(module);
            if (id == kProtectedOverlay) {
                mod.errors.push_back(e.path + ": a overlay da proteção contra cópia não pode ser trocada");
                continue;
            }
            if (ov == overlays.end() || !code) {
                mod.errors.push_back(e.path + ": o jogo não tem a overlay " + std::to_string(id));
                continue;
            }
            std::vector<uint8_t> retail = nitro_overlay_image(rom_image(), *ov);
            std::vector<uint8_t> image;
            if (!pkg.read(e, image, error) || image.empty() || retail.empty()) {
                mod.errors.push_back(image.empty() && error.empty() ? "não foi possível ler " + e.path : error);
                continue;
            }
            if (image.size() > retail.size() + 0x10000) {
                mod.errors.push_back(e.path + " é bem maior que a original (" + std::to_string(image.size()) +
                                     " bytes, a original tem " + std::to_string(retail.size()) + ")");
                continue;
            }
            uint32_t ram_size = uint32_t(std::max(image.size(), retail.size()));
            // Shorter: the rest of the slot keeps the original bytes.
            if (image.size() < retail.size()) image.insert(image.end(), retail.begin() + image.size(), retail.end());
            std::string changed;
            for (uint32_t i = 0; i < code->code_count && changed.empty(); i++) {
                uint32_t start = code->code[2 * i] - ov->ram_address, end = code->code[2 * i + 1] - ov->ram_address;
                for (uint32_t a = start; a < end && a < retail.size(); a++) {
                    if (image[a] != retail[a]) {
                        changed = hex(ov->ram_address + a);
                        break;
                    }
                }
            }
            if (!changed.empty()) {
                mod.errors.push_back(e.path + " muda o código do jogo em " + changed +
                                     " (só os dados das overlays podem mudar)");
                continue;
            }
            h = fnv(h, name.data(), name.size());
            h = fnv(h, image.data(), image.size());
            mod.overlays.push_back({size_t(ov - overlays.begin()), id, std::move(image), ram_size});
        } else if (lpath.find('/') != std::string::npos) {
            mod.warnings.push_back(e.path + " fica fora de files/ e overlays/ (ignorado)");
        }  // files next to mod.ini (a readme, a picture) are not for the game
    }
    if (mod.files.empty() && mod.overlays.empty() && mod.errors.empty()) mod.warnings.push_back("o mod não muda nada");
    mod.fingerprint = h;
    return mod;
}

// A bad name (boej.ini saved in another encoding than UTF-8) makes the file system throw:
// the mod gets an error and the game opens without it.
Mod scan(const std::string& folder, const std::map<std::string, const NitroFile*>& by_path,
         const std::vector<NitroOverlay>& overlays, bool verify) {
    try {
        return scan_mod(folder, by_path, overlays, verify);
    } catch (const std::exception& e) {
        Mod mod;
        mod.folder = folder;
        mod.errors.push_back("não foi possível ler o mod (o boej.ini está em UTF-8?): " + std::string(e.what()));
        return mod;
    }
}

struct Game {
    std::vector<NitroFile> files;
    std::map<std::string, const NitroFile*> by_path;  // lower case
    std::vector<NitroOverlay> overlays;
};

Game read_game() {
    Game g;
    g.files = nitro_files(rom_image());
    for (const NitroFile& f : g.files) g.by_path[lower(f.path)] = &f;
    g.overlays = nitro_overlays(rom_image());
    return g;
}

std::vector<std::string> enabled_list(const Config& config) {
    if (const char* env = SDL_getenv("NTR_MODS")) return split_list(env);  // UTF-8 on Windows too
    return split_list(config.get("mods", "enabled"));
}

// Later mods win: for each file or overlay, which mod gives it, and a warning for the mod
// that lost it.
void resolve(std::vector<Mod>& mods, std::map<uint16_t, const ModFile*>& files,
             std::map<size_t, const ModOverlay*>& overlays) {
    std::map<uint16_t, std::string> file_owner;
    std::map<size_t, std::string> overlay_owner;
    for (Mod& mod : mods) {
        if (!mod.errors.empty()) continue;
        for (const ModFile& f : mod.files) {
            if (auto it = file_owner.find(f.id); it != file_owner.end()) {
                mod.warnings.push_back(f.path + " também está em " + it->second + "; vale o deste mod");
            }
            file_owner[f.id] = mod.folder;
            files[f.id] = &f;
        }
        for (const ModOverlay& o : mod.overlays) {
            if (auto it = overlay_owner.find(o.index); it != overlay_owner.end()) {
                mod.warnings.push_back("a overlay " + std::to_string(o.id) + " também está em " + it->second +
                                       "; vale a deste mod");
            }
            overlay_owner[o.index] = mod.folder;
            overlays[o.index] = &o;
        }
    }
}

}  // namespace

void mods_apply() {
    std::vector<std::string> enabled = enabled_list(host_config());
    if (enabled.empty()) return;
    Game game = read_game();
    if (game.files.empty()) {
        NTR_LOG("[mods] the file table of the game could not be read; mods are off\n");
        return;
    }
    // Mods are scanned once and kept: the game reads their files (mapped, or inflated on
    // demand) while it runs.
    static std::vector<Mod> mods;
    for (const std::string& folder : enabled) mods.push_back(scan(folder, game.by_path, game.overlays, false));
    std::map<uint16_t, const ModFile*> files;
    std::map<size_t, const ModOverlay*> overlays;
    resolve(mods, files, overlays);

    uint64_t h = 0xCBF29CE484222325ull;
    for (const Mod& mod : mods) {
        for (const std::string& w : mod.warnings) NTR_LOG("[mods] %s: %s\n", mod.folder.c_str(), w.c_str());
        for (const std::string& e : mod.errors) NTR_LOG("[mods] %s: error: %s\n", mod.folder.c_str(), e.c_str());
        if (!mod.errors.empty()) {
            NTR_LOG("[mods] %s is off because of the errors above\n", mod.folder.c_str());
            continue;
        }
        NTR_LOG("[mods] %s: %zu files, %zu overlays\n", mod.folder.c_str(), mod.files.size(), mod.overlays.size());
        h = fnv(h, &mod.fingerprint, sizeof mod.fingerprint);
    }
    if (files.empty() && overlays.empty()) return;

    const RomImage& rom = rom_image();
    uint32_t fat_off = rom.read32(0x48), fat_size = rom.read32(0x4C);
    uint32_t ovt_off = rom.read32(0x50), ovt_size = rom.read32(0x54);
    std::vector<uint8_t>& fat = g_owned.emplace_back(rom.bytes(fat_off, fat_size));
    std::vector<uint8_t>& ovt = g_owned.emplace_back(rom.bytes(ovt_off, ovt_size));
    uint64_t cursor = (rom.size() + kAlign - 1) / kAlign * kAlign;
    auto place = [&](uint16_t id, uint32_t size, std::function<const uint8_t*()> load) {
        if (cursor + size > 0xFFFFFFFFull) {
            NTR_LOG("[mods] the mods do not fit in the 4 GiB a cartridge address reaches\n");
            return false;
        }
        store32(&fat[id * 8], uint32_t(cursor));
        store32(&fat[id * 8 + 4], uint32_t(cursor + size));
        rom_override_lazy(uint32_t(cursor), size, std::move(load));
        cursor = (cursor + size + kAlign - 1) / kAlign * kAlign;
        return true;
    };
    // Zipped files are inflated by a thread in the background, from the start; one the game
    // asks for before that thread gets to it is inflated right then (call_once: never twice).
    struct Inflated {
        std::once_flag once;
        std::vector<uint8_t> data;
        std::shared_ptr<ModPackage> package;
        const ModPackage::Entry* entry;
        uint32_t size;
        std::string folder;
        const uint8_t* get() {
            std::call_once(once, [this] {
                std::string error;
                if (!package->read(*entry, data, error) || data.size() != size) {
                    NTR_LOG("[mods] %s: error: %s\n", folder.c_str(), error.c_str());
                    data.assign(size, 0);  // reads as zeros rather than crashing
                }
            });
            return data.data();
        }
    };
    std::vector<std::shared_ptr<Inflated>> zipped;
    for (const Mod& mod : mods) {
        for (const ModFile& f : mod.files) {
            if (auto it = files.find(f.id); it == files.end() || it->second != &f) continue;  // off, or a later mod has it
            std::function<const uint8_t*()> load = [data = f.data] { return data; };
            if (!f.data) {
                auto z = std::make_shared<Inflated>();
                z->package = mod.package;
                z->entry = f.entry;
                z->size = f.size;
                z->folder = mod.folder;
                zipped.push_back(z);
                load = [z] { return z->get(); };
            }
            if (!place(f.id, f.size, std::move(load))) break;
        }
    }
    if (!zipped.empty()) {
        std::thread([zipped] {
            for (const auto& z : zipped) z->get();
        }).detach();
    }
    for (auto [index, o] : overlays) {
        const NitroOverlay& ov = game.overlays[index];
        if (ov.file_id * 8 + 8 > fat.size() ||
            !place(uint16_t(ov.file_id), uint32_t(o->image.size()), [data = o->image.data()] { return data; }))
            break;
        uint8_t* e = &ovt[index * kOverlayEntrySize];
        store32(e + 8, o->ram_size);
        store32(e + 28, 0);  // not compressed
    }
    rom_override(fat_off, fat.data(), fat_size);
    rom_override(ovt_off, ovt.data(), ovt_size);
    g_fingerprint = h;
    NTR_LOG("[mods] %zu files and %zu overlays replaced\n", files.size(), overlays.size());
}

uint64_t mods_fingerprint() { return g_fingerprint; }

// boej --check-mods: every folder and .zip in mods/, for the launcher. One line per mod and per
// problem, tab-separated:
//   mod <folder> <enabled 0|1> <files> <overlays> <errors> <warnings>
//   error <folder> <message>     /    warning <folder> <message>
int mods_check(const std::string& pak_path) {
    log_disable_file();
    if (!rom_load(pak_path)) {
        std::printf("fatal\t\tnão foi possível abrir %s\n", pak_path.c_str());
        return 1;
    }
    Config config;
    const char* config_env = std::getenv("NTR_CONFIG");
    config_load(config_env ? config_env : host_base_path() + "boej.ini", config);
    std::vector<std::string> enabled = enabled_list(config);
    Game game = read_game();

    std::vector<std::string> folders;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(fs_path(mods_dir()), ec)) {
        std::string name = utf8_string(entry.path().filename());
        if (name[0] == '.') continue;
        if (entry.is_directory(ec) || (entry.is_regular_file(ec) && is_zip_name(name))) folders.push_back(name);
    }
    std::sort(folders.begin(), folders.end());
    // Enabled ones first, in their order, so conflicts come out as in the game.
    std::vector<Mod> mods;
    for (const std::string& folder : enabled) mods.push_back(scan(folder, game.by_path, game.overlays, true));
    for (const std::string& folder : folders) {
        if (std::find(enabled.begin(), enabled.end(), folder) == enabled.end()) {
            mods.push_back(scan(folder, game.by_path, game.overlays, true));
        }
    }
    std::vector<Mod> on(mods.begin(), mods.begin() + std::ptrdiff_t(enabled.size()));
    std::map<uint16_t, const ModFile*> files;
    std::map<size_t, const ModOverlay*> overlays;
    resolve(on, files, overlays);
    std::copy(on.begin(), on.end(), mods.begin());
    for (size_t i = 0; i < mods.size(); i++) {
        const Mod& m = mods[i];
        std::printf("mod\t%s\t%d\t%zu\t%zu\t%zu\t%zu\n", m.folder.c_str(), i < enabled.size() ? 1 : 0, m.files.size(),
                    m.overlays.size(), m.errors.size(), m.warnings.size());
        for (const std::string& e : m.errors) std::printf("error\t%s\t%s\n", m.folder.c_str(), e.c_str());
        for (const std::string& w : m.warnings) std::printf("warning\t%s\t%s\n", m.folder.c_str(), w.c_str());
    }
    std::fflush(stdout);
    return 0;
}

}  // namespace ntr
