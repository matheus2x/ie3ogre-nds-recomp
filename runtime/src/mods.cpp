// Mods: one folder or .zip per mod in mods/ next to the executable, turned on in boej.ini
// ([mods] enabled = name, name.zip; a later one wins over an earlier one). Inside each
// (ntr/package.h finds the root of a zip), as described in docs/modding/MODS.md:
//
//   mod.ini              name, version, author, description, game = BOEJ
//   files/<path>         replaces the NitroFS file at <path> (data_iz/...)
//   overlays/ovNNN.bin   overlay NNN as it is in RAM (decompressed); only bytes outside the
//                        recompiled code may change
//   <either>.bps         the same as a BPS patch over the retail file (ntr/package.h): a mod
//                        that carries only its changes, not the game's data
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
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <set>
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
    const uint8_t* data;  // in place (folder or stored zip entry), or null: made when read
    const ModPackage::Entry* entry;
    std::shared_ptr<const std::vector<uint8_t>> patch;  // a .bps, over the retail file at
    uint32_t base_start = 0, base_size = 0;             // these ROM offsets
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
    std::string version;  // from mod.ini, for the log
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

bool ends_with(const std::string& name, const char* suffix) {
    size_t n = std::strlen(suffix);
    return name.size() > n && lower(name.substr(name.size() - n)) == suffix;
}

bool is_zip_name(const std::string& name) { return ends_with(name, ".zip"); }

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
        mod.errors.push_back(L("not in mods/ (a folder or a .zip)", "não existe em mods/ (uma pasta ou um .zip)"));
        return mod;
    }
    if (folder.find(',') != std::string::npos) mod.errors.push_back(L("the name cannot have a comma", "o nome não pode ter vírgula"));
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
        mod.errors.push_back(L("the mod is for another game (", "o mod é para outro jogo (") + ini.get("mod", "game") + ")");
    }
    mod.version = ini.get("mod", "version");
    uint64_t h = fnv(0xCBF29CE484222325ull, folder.data(), folder.size());
    std::map<std::string, std::string> targets;  // what each entry replaces -> the entry

    for (const ModPackage::Entry& e : pkg.entries()) {
        std::string lpath = lower(e.path);
        bool patch = ends_with(lpath, ".bps");
        std::string target = patch ? lpath.substr(0, lpath.size() - 4) : lpath;
        if (auto [it, fresh] = targets.emplace(target, e.path); !fresh) {
            mod.errors.push_back(it->second + L(" and ", " e ") + e.path + L(" replace the same file (keep only one)", " trocam o mesmo arquivo (deixe só um)"));
            continue;
        }
        if (lpath.rfind("files/", 0) == 0) {
            std::string rel = e.path.substr(6, e.path.size() - 6 - (patch ? 4 : 0));
            auto found = by_path.find(lower(rel));
            if (found == by_path.end()) {
                mod.warnings.push_back(e.path + L(" is not in the game (ignored)", " não existe no jogo (ignorado)"));
                continue;
            }
            if (patch) {
                const NitroFile& nf = *found->second;
                auto bytes = std::make_shared<std::vector<uint8_t>>();
                uint64_t source = 0, size = 0;
                if (!pkg.read(e, *bytes, error)) {
                    mod.errors.push_back(error);
                    continue;
                }
                if (!bps_sizes(*bytes, source, size) || size == 0 || size > 0x10000000) {
                    mod.errors.push_back(e.path + L(" is not a valid BPS patch", " não é um patch BPS válido"));
                    continue;
                }
                if (nf.end < nf.start || source != nf.end - nf.start) {
                    mod.errors.push_back(e.path + L(": the patch is for another version of that file", ": o patch é para outra versão desse arquivo"));
                    continue;
                }
                if (verify) {
                    std::vector<uint8_t> base = rom_image().bytes(nf.start, nf.end - nf.start), out;
                    if (!bps_apply(*bytes, base.data(), base.size(), out, error)) {
                        mod.errors.push_back(e.path + ": " + error);
                        continue;
                    }
                }
                mod.files.push_back({nf.path, nf.id, uint32_t(size), nullptr, &e, bytes, nf.start, nf.end - nf.start});
                h = fnv(h, e.path.data(), e.path.size());
                h = fnv(h, &e.size, sizeof e.size);
                h = fnv(h, &e.stamp, sizeof e.stamp);
                continue;
            }
            if (e.size == 0 || e.size > 0x10000000) {
                mod.errors.push_back(e.path + (e.size ? L(" is larger than 256 MiB", " é maior que 256 MiB") : L(" is empty", " está vazio")));
                continue;
            }
            if (verify && pkg.is_zip() && !pkg.read(e, bytes, error)) {
                mod.errors.push_back(error);
                continue;
            }
            const uint8_t* data = pkg.direct(e);
            if (!data && !pkg.is_zip()) {
                mod.errors.push_back(L("could not read ", "não foi possível ler ") + e.path);
                continue;
            }
            mod.files.push_back({found->second->path, found->second->id, uint32_t(e.size), data, &e, nullptr});
            h = fnv(h, e.path.data(), e.path.size());
            h = fnv(h, &e.size, sizeof e.size);
            h = fnv(h, &e.stamp, sizeof e.stamp);
        } else if (lpath.rfind("overlays/", 0) == 0) {
            std::string name = e.path.substr(9, e.path.size() - 9 - (patch ? 4 : 0));
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
                mod.errors.push_back(e.path + L(": the copy protection overlay cannot be replaced", ": a overlay da proteção contra cópia não pode ser trocada"));
                continue;
            }
            if (ov == overlays.end() || !code) {
                mod.errors.push_back(e.path + L(": the game has no overlay ", ": o jogo não tem a overlay ") + std::to_string(id));
                continue;
            }
            std::vector<uint8_t> retail = nitro_overlay_image(rom_image(), *ov);
            std::vector<uint8_t> image;
            if (!pkg.read(e, image, error) || image.empty() || retail.empty()) {
                mod.errors.push_back(image.empty() && error.empty() ? L("could not read ", "não foi possível ler ") + e.path : error);
                continue;
            }
            if (patch) {
                std::vector<uint8_t> bytes = std::move(image);
                if (!bps_apply(bytes, retail.data(), retail.size(), image, error) || image.empty()) {
                    mod.errors.push_back(e.path + ": " + (error.empty() ? std::string(L("the patch is empty", "o patch não tem nada")) : error));
                    continue;
                }
            }
            if (image.size() > retail.size() + 0x10000) {
                mod.errors.push_back(e.path + L(" is much larger than the original (", " é bem maior que a original (") +
                                     std::to_string(image.size()) + L(" bytes, the original has ", " bytes, a original tem ") +
                                     std::to_string(retail.size()) + ")");
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
                mod.errors.push_back(e.path + L(" changes the game's code at ", " muda o código do jogo em ") + changed +
                                     L(" (only the overlays' data can change)", " (só os dados das overlays podem mudar)"));
                continue;
            }
            h = fnv(h, name.data(), name.size());
            h = fnv(h, image.data(), image.size());
            mod.overlays.push_back({size_t(ov - overlays.begin()), id, std::move(image), ram_size});
        } else if (lpath.find('/') != std::string::npos) {
            mod.warnings.push_back(e.path + L(" is outside files/ and overlays/ (ignored)", " fica fora de files/ e overlays/ (ignorado)"));
        }  // files next to mod.ini (a readme, a picture) are not for the game
    }
    if (mod.files.empty() && mod.overlays.empty() && mod.errors.empty()) mod.warnings.push_back(L("the mod changes nothing", "o mod não muda nada"));
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
        mod.errors.push_back(L("could not read the mod (is boej.ini in UTF-8?): ", "não foi possível ler o mod (o boej.ini está em UTF-8?): ") +
                                 std::string(e.what()));
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
                mod.warnings.push_back(f.path + L(" is also in ", " também está em ") + it->second + L("; this mod's wins", "; vale o deste mod"));
            }
            file_owner[f.id] = mod.folder;
            files[f.id] = &f;
        }
        for (const ModOverlay& o : mod.overlays) {
            if (auto it = overlay_owner.find(o.index); it != overlay_owner.end()) {
                mod.warnings.push_back(L("overlay ", "a overlay ") + std::to_string(o.id) + L(" is also in ", " também está em ") +
                                       it->second + L("; this mod's wins", "; vale a deste mod"));
            }
            overlay_owner[o.index] = mod.folder;
            overlays[o.index] = &o;
        }
    }
}

// --- texts kept in the save ------------------------------------------------------------
// The save keeps the place, the label ("もくてき") and the objective of the top screen as
// text, not as an index: a save made with a translation shows its letters as "?" without it
// (the original font has no such codes), and an older save shows Japanese with it. Those texts
// come from instructions of the event scripts (data_iz/script/eve.pkb), so every version of
// them is known: the game's and that of each mod in mods/, on or off. A text that belongs to
// none of the scripts in use is swapped for the one the same instruction has in them
// (saved_text_fix, called where the top screen draws it).

constexpr const char* kEventIndex = "data_iz/script/eve.pkh";
constexpr const char* kEvents = "data_iz/script/eve.pkb";

struct SavedTexts {
    std::map<std::string, std::string> replace;  // text of another version -> the one in use
    std::set<std::string> in_use;
};
std::shared_future<std::shared_ptr<const SavedTexts>> g_saved_texts;

bool lz10(const std::vector<uint8_t>& in, size_t at, size_t n, std::vector<uint8_t>& out) {
    out.clear();
    if (n < 4 || at + n > in.size() || in[at] != 0x10) return false;
    const uint8_t* p = in.data() + at;
    size_t size = load32(p) >> 8, i = 4;
    out.reserve(size);
    while (out.size() < size) {
        if (i >= n) return false;
        uint8_t flags = p[i++];
        for (int bit = 0; bit < 8 && out.size() < size; bit++, flags <<= 1) {
            if (!(flags & 0x80)) {
                if (i >= n) return false;
                out.push_back(p[i++]);
                continue;
            }
            if (i + 2 > n) return false;
            size_t len = (p[i] >> 4) + 3, disp = ((p[i] & 0xF) << 8 | p[i + 1]) + 1;
            i += 2;
            if (disp > out.size()) return false;
            for (size_t k = 0; k < len && out.size() < size; k++) out.push_back(out[out.size() - disp]);
        }
    }
    return true;
}

// (event, instruction id, record) -> text, for the records that end up in the save: the
// place (record 3 of opcode 0x4037; record 2 is the map), the label (record 2 of 0x402F) and
// the objective (record 3 of 0x201C and 0x201D). Scripts: [u16 id][u16 size][u16 opcode]...;
// records: [u16 id][u8 index][u8 size][text, NUL] (tools/ntr/text.py).
std::map<uint64_t, std::string> saved_strings(const std::vector<uint8_t>& pkh, const std::vector<uint8_t>& pkb) {
    std::map<uint64_t, std::string> out;
    if (pkh.size() < 0x30 || std::memcmp(pkh.data(), "PackNum", 7) != 0) return out;
    std::vector<uint8_t> d;
    for (size_t at = 0x30; at + 12 <= pkh.size(); at += 12) {
        uint32_t key = load32(&pkh[at]), off = load32(&pkh[at + 4]), size = load32(&pkh[at + 8]);
        if (!lz10(pkb, off, size, d) || d.size() < 0x20 || std::memcmp(d.data(), "SSD\0", 4) != 0) continue;
        size_t code = load32(&d[16]), data = load32(&d[20]);
        if (0x20 + code > d.size()) continue;
        std::map<uint16_t, uint16_t> ops;
        for (size_t j = 0x20; j + 6 <= 0x20 + code;) {
            uint16_t id = uint16_t(d[j] | d[j + 1] << 8), n = uint16_t(d[j + 2] | d[j + 3] << 8);
            if (n < 4) break;
            ops.emplace(id, uint16_t(d[j + 4] | d[j + 5] << 8));
            j += n;
        }
        size_t end = std::min(d.size(), 0x20 + code + data);
        for (size_t i = 0x20 + code; i + 4 <= end;) {
            uint16_t id = uint16_t(d[i] | d[i + 1] << 8);
            uint8_t index = d[i + 2], n = d[i + 3];
            if (n < 4) break;
            auto op = ops.find(id);
            uint16_t o = op == ops.end() ? 0 : op->second;
            if ((o == 0x4037 && index == 3) || (o == 0x402F && index == 2) || ((o == 0x201C || o == 0x201D) && index == 3)) {
                const char* s = reinterpret_cast<const char*>(&d[i + 4]);
                out[uint64_t(key) << 24 | uint64_t(id) << 8 | index] = std::string(s, strnlen(s, std::min<size_t>(n - 4, end - i - 4)));
            }
            i += n;
        }
    }
    return out;
}

// The file as a mod has it (whole or as a patch over the game's); false if the mod does not
// change it.
bool mod_file_bytes(const ModPackage& pkg, const NitroFile& nf, std::vector<uint8_t>& out) {
    std::string error;
    if (const ModPackage::Entry* e = pkg.find("files/" + nf.path)) return pkg.read(*e, out, error);
    const ModPackage::Entry* e = pkg.find("files/" + nf.path + ".bps");
    std::vector<uint8_t> patch;
    if (!e || !pkg.read(*e, patch, error)) return false;
    std::vector<uint8_t> base = rom_image().bytes(nf.start, nf.end - nf.start);
    return bps_apply(patch, base.data(), base.size(), out, error);
}

std::shared_ptr<const SavedTexts> build_saved_texts(const std::vector<std::string>& in_use) {
    auto texts = std::make_shared<SavedTexts>();
    std::vector<NitroFile> files = nitro_files(rom_image());
    auto find = [&](const char* path) {
        auto it = std::find_if(files.begin(), files.end(), [&](const NitroFile& f) { return lower(f.path) == path; });
        return it == files.end() ? nullptr : &*it;
    };
    const NitroFile *index = find(kEventIndex), *events = find(kEvents);
    if (!index || !events) return texts;
    struct Version {
        std::vector<uint8_t> pkh, pkb;  // empty: the game's
    };
    const Version game{rom_image().bytes(index->start, index->end - index->start),
                       rom_image().bytes(events->start, events->end - events->start)};
    std::map<std::string, Version> mods;  // every mod in mods/ that changes the scripts
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(fs_path(mods_dir()), ec)) {
        std::string name = utf8_string(entry.path().filename());
        if (name[0] == '.' || !(entry.is_directory(ec) || (entry.is_regular_file(ec) && is_zip_name(name)))) continue;
        ModPackage pkg;
        std::string error;
        Version v;
        if (!pkg.open(mods_dir() + name, error)) continue;
        bool has_pkh = mod_file_bytes(pkg, *index, v.pkh), has_pkb = mod_file_bytes(pkg, *events, v.pkb);
        if (has_pkh || has_pkb) mods[name] = std::move(v);
    }
    auto strings = [&](const Version& v) {
        return saved_strings(v.pkh.empty() ? game.pkh : v.pkh, v.pkb.empty() ? game.pkb : v.pkb);
    };
    // In use: a later mod wins, file by file, as in mods_apply.
    Version use;
    for (const std::string& folder : in_use) {
        if (auto it = mods.find(folder); it != mods.end()) {
            if (!it->second.pkh.empty()) use.pkh = it->second.pkh;
            if (!it->second.pkb.empty()) use.pkb = it->second.pkb;
        }
    }
    std::map<uint64_t, std::string> now = strings(use);
    for (const auto& [key, text] : now) texts->in_use.insert(text);
    std::vector<std::map<uint64_t, std::string>> versions{strings(game)};
    for (const auto& [folder, v] : mods) versions.push_back(strings(v));
    for (const auto& version : versions) {
        for (const auto& [key, text] : version) {
            auto it = now.find(key);
            if (it != now.end() && !texts->in_use.count(text)) texts->replace.emplace(text, it->second);
        }
    }
    return texts;
}

void saved_texts_start(std::vector<std::string> in_use) {
    g_saved_texts = std::async(std::launch::async, [in_use = std::move(in_use)]() -> std::shared_ptr<const SavedTexts> {
        try {
            auto texts = build_saved_texts(in_use);
            NTR_LOG("[mods] texts kept in saves: %zu of other versions known\n", texts->replace.size());
            return texts;
        } catch (const std::exception& e) {  // a bad file name in mods/
            NTR_LOG("[mods] texts kept in saves: %s\n", e.what());
            return std::make_shared<SavedTexts>();
        }
    }).share();
}

// The mods in use (no errors), in order.
std::vector<std::string> apply_mods() {
    std::vector<std::string> enabled = enabled_list(host_config());
    if (enabled.empty()) return {};
    Game game = read_game();
    if (game.files.empty()) {
        NTR_LOG("[mods] the file table of the game could not be read; mods are off\n");
        return {};
    }
    // Mods are scanned once and kept: the game reads their files (mapped, or inflated on
    // demand) while it runs.
    static std::vector<Mod> mods;
    for (const std::string& folder : enabled) mods.push_back(scan(folder, game.by_path, game.overlays, false));
    std::map<uint16_t, const ModFile*> files;
    std::map<size_t, const ModOverlay*> overlays;
    resolve(mods, files, overlays);

    uint64_t h = 0xCBF29CE484222325ull;
    std::string used;
    std::vector<std::string> in_use;
    for (const Mod& mod : mods) {
        for (const std::string& w : mod.warnings) NTR_LOG("[mods] %s: %s\n", mod.folder.c_str(), w.c_str());
        for (const std::string& e : mod.errors) NTR_LOG("[mods] %s: error: %s\n", mod.folder.c_str(), e.c_str());
        if (!mod.errors.empty()) {
            NTR_LOG("[mods] %s is off because of the errors above\n", mod.folder.c_str());
            continue;
        }
        NTR_LOG("[mods] %s%s%s: %zu files, %zu overlays\n", mod.folder.c_str(), mod.version.empty() ? "" : " v",
                mod.version.c_str(), mod.files.size(), mod.overlays.size());
        h = fnv(h, &mod.fingerprint, sizeof mod.fingerprint);
        in_use.push_back(mod.folder);
        used += (used.empty() ? "" : ", ") + mod.folder + (mod.version.empty() ? "" : " v" + mod.version);
    }
    session_info("mods", used.empty() ? "nenhum" : used);
    if (files.empty() && overlays.empty()) return in_use;

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
    // Zipped files are inflated, and patches applied, by a thread in the background, from the
    // start; one the game asks for before that thread gets to it is made right then
    // (call_once: never twice).
    struct Inflated {
        std::once_flag once;
        std::vector<uint8_t> data;
        std::shared_ptr<ModPackage> package;
        const ModFile* file;
        std::string folder;
        const uint8_t* get() {
            std::call_once(once, [this] {
                std::string error;
                bool ok;
                if (file->patch) {
                    std::vector<uint8_t> base = rom_image().bytes(file->base_start, file->base_size);
                    ok = bps_apply(*file->patch, base.data(), base.size(), data, error);
                    if (!ok) error = file->entry->path + ": " + error;
                } else {
                    ok = package->read(*file->entry, data, error);
                }
                if (!ok || data.size() != file->size) {
                    NTR_LOG("[mods] %s: error: %s\n", folder.c_str(), error.c_str());
                    data.assign(file->size, 0);  // reads as zeros rather than crashing
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
                z->file = &f;
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
    return in_use;
}

}  // namespace

void mods_apply() { saved_texts_start(apply_mods()); }

void saved_text_fix(uint32_t text, uint32_t copy, uint32_t size) {
    if (!g_saved_texts.valid()) return;
    std::shared_ptr<const SavedTexts> texts = g_saved_texts.get();  // made in the background from the start
    std::string old;
    for (uint32_t i = 0; i < size; i++) {
        char ch = char(rd8(text + i));
        if (!ch) break;
        old += ch;
    }
    if (old.empty() || texts->in_use.count(old)) return;
    auto it = texts->replace.find(old);
    if (it == texts->replace.end() || it->second.size() >= size) return;
    for (uint32_t at : {text, copy}) {
        bool same = true;
        for (uint32_t i = 0; i <= old.size() && same; i++) same = rd8(at + i) == uint8_t(old.c_str()[i]);
        if (!same) continue;
        for (uint32_t i = 0; i <= it->second.size(); i++) wr8(at + i, uint8_t(it->second.c_str()[i]));
    }
    log_detail("[mods] a text kept in the save (%zu bytes) is now the one of the scripts in use (%zu bytes)\n", old.size(),
               it->second.size());
}


uint64_t mods_fingerprint() { return g_fingerprint; }

// boej --check-mods: every folder and .zip in mods/, for the launcher. One line per mod and per
// problem, tab-separated:
//   mod <folder> <enabled 0|1> <files> <overlays> <errors> <warnings>
//   error <folder> <message>     /    warning <folder> <message>
int mods_check(const std::string& pak_path) {
    log_disable_file();
    Config config;
    const char* config_env = std::getenv("NTR_CONFIG");
    config_load(config_env ? config_env : host_base_path() + "boej.ini", config);
    set_language(config);  // the launcher shows these messages
    if (!rom_load(pak_path)) {
        std::printf("fatal\t\t%s %s\n", L("could not open", "não foi possível abrir"), pak_path.c_str());
        return 1;
    }
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
