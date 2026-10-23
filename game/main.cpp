// boej: runs the recompiled Inazuma Eleven 3 (BOEJ).
//
// Usage: boej [boej.pak | rom.nds]
//
// The game data comes from boej.pak, looked up next to the executable, then in the
// current directory. On the first run there is none: the game asks for the player's
// dump (or takes the .nds given on the command line, which is what dropping it on the
// executable does) and makes the package next to the executable. With the package in
// place, a .nds given on the command line is run directly.
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

#include <ntr/runtime.h>

namespace {

std::filesystem::path utf8_path(const std::string& s) { return std::u8string(s.begin(), s.end()); }

// UTF-8 arguments: on Windows argv is in the ANSI code page, which loses characters
// outside it (a dump in C:\Users\<name with accents>, for example).
std::vector<std::string> arguments(int argc, char** argv) {
#ifdef _WIN32
    (void)argv;
    std::vector<std::string> args;
    LPWSTR* wargv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 0; wargv && i < argc; i++) {
        int len = WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, nullptr, 0, nullptr, nullptr);
        std::string arg(len > 0 ? len - 1 : 0, '\0');
        WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, arg.data(), len, nullptr, nullptr);
        args.push_back(arg);
    }
    LocalFree(wargv);
    return args;
#else
    return std::vector<std::string>(argv, argv + argc);
#endif
}

bool is_dump(const std::string& path) {
    std::string ext = utf8_path(path).extension().string();
    for (char& c : ext) c = char(std::tolower(static_cast<unsigned char>(c)));
    return ext == ".nds";
}

std::string find_pak() {
    for (const std::string& candidate : {ntr::host_base_path() + "boej.pak", std::string("boej.pak")}) {
        std::error_code ec;
        if (std::filesystem::is_regular_file(utf8_path(candidate), ec)) return candidate;
    }
    return "";
}

}  // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    // Started from Explorer, the console window is ours alone: hide it (everything also
    // goes to boej.log). From a terminal, the output stays there. NTR_CONSOLE keeps it.
    DWORD processes[2];
    if (GetConsoleProcessList(processes, 2) == 1 && !std::getenv("NTR_CONSOLE")) FreeConsole();
#endif
    std::vector<std::string> args = arguments(argc, argv);
    if (args.size() > 2) {
        std::fprintf(stderr, "usage: boej [boej.pak | rom.nds]\n");
        return 2;
    }
    // The window comes first: the first run needs it to ask for the dump.
    if (!ntr::host_init()) {
        ntr::log_printf("error: cannot open the window (set NTR_HEADLESS=1 to run without one)\n");
        return 1;
    }
    std::string path = args.size() == 2 ? args[1] : find_pak();
    if (path.empty() || (is_dump(path) && find_pak().empty())) {
        std::string pak = ntr::host_base_path() + "boej.pak";
        if (ntr::host_first_run(path, pak)) {
            path = pak;
        } else if (path.empty()) {
            ntr::log_printf(
                "error: boej.pak not found next to the executable.\n"
                "Run the game with a window to create it from your dump, or use: python3 tools/extract.py <rom.nds>\n");
            return 1;
        }
    }
    if (!ntr::rom_load(path)) {
        ntr::log_printf("error: cannot read %s\n", path.c_str());
        return 1;
    }
    ntr::boot();
}
