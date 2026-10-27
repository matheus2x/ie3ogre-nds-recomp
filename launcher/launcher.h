// The launcher: prepares the game on the player's PC and starts it.
//
// A release carries no game data and no game code. On the first run the launcher checks
// the player's dump, makes the game data package (boej.pak), extracts the ARM9 code
// modules with dsd, recompiles them with ntr-recomp and compiles boej.exe with a portable
// compiler (llvm-mingw, downloaded once and checked against a pinned SHA-256). Everything
// lives next to the launcher:
//
//   ie3-launcher(.exe), SDL3.dll       the release
//   sdk/                               the release: tools, runtime sources, manifest
//   toolchain/                         the compiler (Windows), downloaded on first use
//   cache/modules/                     code modules extracted from the dump
//   boej(.exe), boej.pak               made on the player's PC
//   boej.ini, saves/, mods/            settings and player data
//   logs/                              launcher.log, historico.txt (every game session),
//                                      sessoes/ and replays/ (log and input of the last 20)
#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include <ntr/common.h>

struct SDL_Window;

namespace launcher {

#ifndef LAUNCHER_VERSION
#define LAUNCHER_VERSION "0.0.0-dev"
#endif

// --- paths -------------------------------------------------------------------------------

struct Paths {
    std::string root;  // the launcher's folder, with a trailing separator
    std::string sdk() const { return root + "sdk/"; }
    std::string cache() const { return root + "cache/"; }
    std::string modules() const { return cache() + "modules/"; }
    std::string toolchain() const { return root + "toolchain/"; }
    std::string game() const;      // boej.exe / boej
    std::string launcher() const;  // ie3-launcher.exe / ie3-launcher
    std::string pak() const { return root + "boej.pak"; }
    std::string config() const { return root + "boej.ini"; }
    std::string mods() const { return root + "mods/"; }
    std::string logs() const { return root + "logs/"; }
};
const Paths& paths();
std::string sdk_version();  // sdk/version.txt, or "" without an SDK

// --- processes (process.cpp) ---------------------------------------------------------

// Runs a program without a console window and waits; stdout and stderr go to `output`.
// Returns the exit code, or -1 if it could not start.
int run_process(const std::vector<std::string>& args, std::string& output, const std::string& cwd = "");
// Starts a program and does not wait (the game). With `on_exit`, a thread waits for the
// program and calls it when the program ends.
bool start_process(const std::vector<std::string>& args, const std::string& cwd, std::function<void()> on_exit = {});
std::string system_tool(const char* name);  // curl / tar: System32 on Windows, PATH elsewhere

// --- preparing the game (prepare.cpp) -------------------------------------------------

enum class Readiness {
    NoSdk,       // a source checkout without tools/build.py --sdk
    NeedsDump,   // no boej.pak or no code modules: the dump is needed once
    NeedsBuild,  // boej.exe missing or made by another release
    Ready,
};
Readiness readiness();

// Progress of a preparation running on a worker thread.
struct Job {
    std::atomic<bool> running{false}, finished{false}, ok{false}, cancel{false};
    std::mutex mutex;
    std::string step;      // what is happening, for the player
    double progress = 0;   // of the current step, 0..1 (negative = unknown)
    std::string error;     // message for the player when !ok
    std::vector<std::string> log;

    void set_step(const std::string& s, double p = 0);
    void set_progress(double p);
    void add_log(const std::string& line);
};

// Starts the preparation: with `dump` (a .nds path) it first checks the dump and makes
// boej.pak and the code modules, then builds the game if needed.
void prepare_start(Job& job, const std::string& dump);

// --- settings (settings.cpp) -----------------------------------------------------------

void settings_load();
void settings_ui();
bool settings_capturing();  // waiting for a key or gamepad input to bind
void settings_event(const union SDL_Event& ev);
const ntr::Config& settings();
void settings_set(const std::string& section, const std::string& key, const std::string& value);  // and saves

// --- mods (mods.cpp) ---------------------------------------------------------------------

void mods_tab(SDL_Window* window);
void mods_drop(const std::string& path);  // a .zip or a mod folder dropped on the window: copy it to mods/
int mods_cli(const std::string& command, const std::string& path);  // --install-mod / --mod-from-dump

// --- updates (update.cpp) --------------------------------------------------------------

void update_check_start();  // in the background
void update_ui();
bool update_available();
void update_cleanup();  // leftovers of the previous update, at startup
int update_cli();       // ie3-launcher --update: check, download and install

// --- helpers ----------------------------------------------------------------------------

std::string format_bytes(uint64_t bytes);
int compare_versions(const std::string& a, const std::string& b);  // <0, 0, >0
void open_folder(const std::string& path);

}  // namespace launcher
