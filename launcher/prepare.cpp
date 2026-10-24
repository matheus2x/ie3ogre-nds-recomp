// Preparing the game on the player's PC: dump -> boej.pak and code modules (once), then
// code modules -> ntr-recomp -> C++ -> boej.exe (again after each release).
#include <algorithm>
#include <chrono>
#include <fstream>
#include <sstream>
#include <thread>

#include "launcher.h"

namespace fs = std::filesystem;

namespace launcher {

namespace {

#ifdef _WIN32
constexpr const char* kExe = ".exe";
#else
constexpr const char* kExe = "";
#endif

bool exists(const std::string& path) {
    std::error_code ec;
    return fs::exists(ntr::fs_path(path), ec);
}

std::string read_text(const std::string& path) {
    std::ifstream in(ntr::fs_path(path), std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool write_text(const std::string& path, const std::string& text) {
    std::ofstream out(ntr::fs_path(path), std::ios::binary);
    out << text;
    return bool(out);
}

std::string trim(std::string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
    return s;
}

// "key=value" lines (sdk/toolchain.txt).
[[maybe_unused]] std::string value_of(const std::string& text, const std::string& key) {
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind(key + "=", 0) == 0) return trim(line.substr(key.size() + 1));
    }
    return "";
}

// The compiler: llvm-mingw under toolchain/ on Windows, the system's c++ elsewhere.
struct Toolchain {
    std::string dir;  // "" = system compiler
    std::string cxx;
};

Toolchain toolchain_spec() {
#ifdef _WIN32
    std::string dir = value_of(read_text(paths().sdk() + "toolchain.txt"), "dir");
    return {dir, paths().toolchain() + dir + "/bin/x86_64-w64-mingw32-clang++.exe"};
#else
    const char* cxx = std::getenv("CXX");
    return {"", cxx ? cxx : "c++"};
#endif
}

std::string build_stamp() {
    Toolchain tc = toolchain_spec();
    return sdk_version() + " " + (tc.dir.empty() ? tc.cxx : tc.dir);
}

bool remove_all(const std::string& path) {
    std::error_code ec;
    fs::remove_all(ntr::fs_path(path), ec);
    return !ec;
}

[[maybe_unused]] std::string first_lines(const std::string& text, int count) {
    std::string out;
    std::istringstream in(text);
    std::string line;
    while (count-- > 0 && std::getline(in, line)) out += line + "\n";
    return out;
}

double seconds_since(std::chrono::steady_clock::time_point t) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t).count();
}

// --- steps ---------------------------------------------------------------------------

// Dump -> boej.pak (checks the SHA-1 first) and the code modules dsd extracts, ov129
// with its DS Protect functions decrypted.
bool import_dump(Job& job, const std::string& dump) {
    job.set_step("Conferindo o dump e criando o boej.pak");
    std::string error;
    if (!ntr::pak_create(dump, paths().pak(), error, [&](double p) { job.set_progress(p); })) {
        job.error = error;
        return false;
    }
    job.set_step("Extraindo o código do jogo", -1);
    std::string extract = paths().cache() + "extract";
    remove_all(extract);
    std::string out;
    int code = run_process({paths().sdk() + "bin/dsd" + kExe, "rom", "extract", "--rom", dump, "--output-path", extract}, out);
    if (code != 0) {
        job.add_log(out);
        job.error = "A extração do código (dsd) falhou. Veja os detalhes no launcher.log.";
        NTR_LOG("[prepare] dsd failed (%d):\n%s\n", code, out.c_str());
        return false;
    }
    std::string modules = paths().modules();
    remove_all(modules);
    std::error_code ec;
    fs::create_directories(ntr::fs_path(modules), ec);
    fs::rename(ntr::fs_path(extract + "/arm9"), ntr::fs_path(modules + "arm9"), ec);
    if (!ec) fs::rename(ntr::fs_path(extract + "/arm9_overlays"), ntr::fs_path(modules + "arm9_overlays"), ec);
    remove_all(extract);  // the rest is the file system, already in boej.pak
    if (ec || !write_text(modules + "ok", sdk_version() + "\n")) {
        job.error = "Não foi possível gravar em " + modules + ".";
        return false;
    }
    return true;
}

#ifdef _WIN32
// Downloads llvm-mingw once (pinned in sdk/toolchain.txt) and checks its SHA-256.
bool ensure_toolchain(Job& job) {
    Toolchain tc = toolchain_spec();
    if (exists(tc.cxx)) return true;
    std::string spec = read_text(paths().sdk() + "toolchain.txt");
    std::string url = value_of(spec, "url"), sha256 = value_of(spec, "sha256");
    uint64_t size = std::strtoull(value_of(spec, "size").c_str(), nullptr, 10);
    std::string zip = paths().toolchain() + "download.zip";
    std::error_code ec;
    fs::create_directories(ntr::fs_path(paths().toolchain()), ec);
    if (ntr::sha256_file(zip) != sha256) {  // a zip put there by hand is used as is
        job.set_step("Baixando o compilador (" + format_bytes(size) + ", só na primeira vez)");
        std::atomic<bool> done{false};
        std::string out;
        int code = 0;
        std::thread curl([&] {
            code = run_process({system_tool("curl"), "-L", "--fail", "--silent", "--show-error", "--retry", "3", "-o", zip, url}, out);
            done = true;
        });
        while (!done) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            std::error_code e;
            uintmax_t now = fs::file_size(ntr::fs_path(zip), e);
            if (!e && size) job.set_progress(double(now) / double(size));
        }
        curl.join();
        if (code != 0) {
            NTR_LOG("[prepare] download failed (%d): %s\n", code, out.c_str());
            job.error = "Não foi possível baixar o compilador:\n" + first_lines(out, 3) +
                        "\nConfira a conexão. Sem internet, baixe " + url + " e coloque o arquivo em " + zip + ".";
            return false;
        }
        job.set_step("Conferindo o download", -1);
        if (ntr::sha256_file(zip) != sha256) {
            job.error = "O compilador baixado está corrompido (SHA-256 diferente). Tente de novo.";
            fs::remove(ntr::fs_path(zip), ec);
            return false;
        }
    }
    job.set_step("Instalando o compilador", -1);
    std::string out;
    // Only the x86-64 target: the other three (~210 MB) are never used.
    int code = run_process({system_tool("tar"), "-xf", zip, "-C", paths().toolchain(), "--exclude", "*/armv7-w64-mingw32/*",
                            "--exclude", "*/i686-w64-mingw32/*", "--exclude", "*/aarch64-w64-mingw32/*"},
                           out);
    if (code != 0 || !exists(tc.cxx)) {
        NTR_LOG("[prepare] tar failed (%d): %s\n", code, out.c_str());
        job.error = "Não foi possível extrair o compilador.\n" + first_lines(out, 3);
        return false;
    }
    fs::remove(ntr::fs_path(zip), ec);
    return true;
}
#endif

struct CompileJob {
    std::vector<std::string> args;
    std::string source;
};

// Runs the jobs on every core; stops at the first failure.
bool run_jobs(Job& job, const std::vector<CompileJob>& jobs, std::string& failure) {
    std::atomic<size_t> next{0}, done{0};
    std::atomic<bool> failed{false};
    std::mutex failure_mutex;
    unsigned workers = std::max(1u, std::thread::hardware_concurrency());
    std::vector<std::thread> threads;
    for (unsigned w = 0; w < workers; w++) {
        threads.emplace_back([&] {
            for (;;) {
                size_t i = next++;
                if (i >= jobs.size() || failed || job.cancel) return;
                std::string out;
                int code = run_process(jobs[i].args, out);
                if (code != 0) {
                    std::lock_guard lock(failure_mutex);
                    if (!failed) failure = jobs[i].source + ":\n" + out;
                    failed = true;
                    return;
                }
                job.set_progress(double(++done) / double(jobs.size()));
            }
        });
    }
    for (std::thread& t : threads) t.join();
    return !failed && !job.cancel;
}

bool build_game(Job& job) {
    auto start = std::chrono::steady_clock::now();
    Toolchain tc = toolchain_spec();
    std::string sdk = paths().sdk(), work = paths().cache() + "build/";
    remove_all(work);
    std::error_code ec;
    fs::create_directories(ntr::fs_path(work + "obj"), ec);

    // The manifest names the modules as {modules}/...: they live on this PC only.
    std::string manifest = read_text(sdk + "manifest.tsv");
    std::string modules = paths().modules();
    if (!modules.empty() && modules.back() == '/') modules.pop_back();
    for (size_t at; (at = manifest.find("{modules}")) != std::string::npos;) manifest.replace(at, 9, modules);
    write_text(work + "manifest.tsv", manifest);

    job.set_step("Recompilando o código do jogo (ARM → C++)", -1);
    std::string out;
    int code = run_process({sdk + "bin/ntr-recomp" + kExe, work + "manifest.tsv", work + "gen"}, out);
    if (code != 0) {
        NTR_LOG("[prepare] ntr-recomp failed (%d):\n%s\n", code, out.c_str());
        job.error = "A recompilação falhou. Veja os detalhes no launcher.log.";
        return false;
    }
    NTR_LOG("[prepare] recompiled in %.1f s\n", seconds_since(start));

    // Savestates restore stack frames from another run: no stack canaries (they change in
    // every run) and, at link time, a fixed load address. Keep in sync with game/CMakeLists.txt.
    std::vector<std::string> common = {tc.cxx, "-std=c++20", "-fno-stack-protector", "-I" + sdk + "include",
                                       "-I" + sdk + "imgui"};
#ifdef _WIN32
    common.push_back("-I" + sdk + "sdl3/include");
#endif
    std::vector<CompileJob> jobs;
    std::vector<std::string> objects;
    auto add = [&](const std::string& source, const std::string& name, std::vector<std::string> flags) {
        std::string obj = work + "obj/" + name + ".o";
        std::vector<std::string> args = common;
        args.insert(args.end(), flags.begin(), flags.end());
        args.insert(args.end(), {"-c", source, "-o", obj});
        jobs.push_back({args, source});
        objects.push_back(obj);
    };
    // Generated code first: it is most of the work, so the cores stay busy to the end.
    for (const auto& entry : fs::directory_iterator(ntr::fs_path(work + "gen"))) {
        if (entry.path().extension() != ".cpp") continue;
        add(ntr::utf8_string(entry.path()), "gen_" + ntr::utf8_string(entry.path().stem()),
            {"-O1", "-w", "-fno-strict-aliasing", "-I" + work + "gen"});
    }
    for (const auto& entry : fs::directory_iterator(ntr::fs_path(sdk + "runtime"))) {
        if (entry.path().extension() != ".cpp") continue;
        add(ntr::utf8_string(entry.path()), "rt_" + ntr::utf8_string(entry.path().stem()), {"-O2", "-DNDEBUG"});
    }
    for (const char* dir : {"ui", "imgui"}) {  // the pause menu
        for (const auto& entry : fs::directory_iterator(ntr::fs_path(sdk + dir))) {
            if (entry.path().extension() != ".cpp") continue;
            add(ntr::utf8_string(entry.path()), std::string(dir) + "_" + ntr::utf8_string(entry.path().stem()),
                {"-O2", "-DNDEBUG"});
        }
    }
    add(sdk + "game/main.cpp", "main", {"-O2", "-DNDEBUG"});

    job.set_step("Compilando o jogo (" + std::to_string(jobs.size()) + " arquivos, " +
                 std::to_string(std::max(1u, std::thread::hardware_concurrency())) + " núcleos)");
    std::string failure;
    auto compile_start = std::chrono::steady_clock::now();
    if (!run_jobs(job, jobs, failure)) {
        if (job.cancel) return false;
        NTR_LOG("[prepare] compile failed:\n%s\n", failure.c_str());
        job.add_log(failure);
        job.error = "A compilação falhou. Veja os detalhes no launcher.log.";
        return false;
    }
    NTR_LOG("[prepare] compiled %zu files in %.1f s\n", jobs.size(), seconds_since(compile_start));

    job.set_step("Ligando o executável", -1);
    std::string rsp;
    for (std::string obj : objects) {  // the response file treats backslashes as escapes
        std::replace(obj.begin(), obj.end(), '\\', '/');
        rsp += "\"" + obj + "\"\n";
    }
    write_text(work + "objects.rsp", rsp);
    std::string target = paths().game();
    std::vector<std::string> link = {tc.cxx, "-o", target + ".tmp", "@" + work + "objects.rsp"};
#ifdef _WIN32
    link.insert(link.end(), {"-static", sdk + "sdl3/lib/libSDL3.dll.a", "-lshell32", "-Wl,--disable-dynamicbase",
                             "-Wl,--large-address-aware"});
#else
    link.insert(link.end(), {"-lSDL3", "-pthread", "-no-pie"});
#endif
    out.clear();
    code = run_process(link, out);
    if (code != 0) {
        NTR_LOG("[prepare] link failed (%d):\n%s\n", code, out.c_str());
        job.add_log(out);
        job.error = "A ligação do executável falhou. Veja os detalhes no launcher.log.";
        return false;
    }
    fs::remove(ntr::fs_path(target), ec);
    fs::rename(ntr::fs_path(target + ".tmp"), ntr::fs_path(target), ec);
    if (ec) {
        job.error = "Não foi possível substituir " + target + " (o jogo está aberto?).";
        return false;
    }
    write_text(paths().cache() + "build.stamp", build_stamp() + "\n");
    remove_all(work);
    NTR_LOG("[prepare] built %s in %.1f s\n", target.c_str(), seconds_since(start));
    return true;
}

}  // namespace

std::string Paths::game() const { return root + "boej" + kExe; }

std::string Paths::launcher() const { return root + "ie3-launcher" + kExe; }

std::string sdk_version() { return trim(read_text(paths().sdk() + "version.txt")); }

Readiness readiness() {
    if (sdk_version().empty()) return Readiness::NoSdk;
    if (!exists(paths().pak()) || !exists(paths().modules() + "ok")) return Readiness::NeedsDump;
    if (!exists(paths().game()) || trim(read_text(paths().cache() + "build.stamp")) != build_stamp()) {
        return Readiness::NeedsBuild;
    }
    return Readiness::Ready;
}

void Job::set_step(const std::string& s, double p) {
    std::lock_guard lock(mutex);
    step = s;
    progress = p;
    log.push_back("» " + s);
    NTR_LOG("[prepare] %s\n", s.c_str());
}

void Job::set_progress(double p) {
    std::lock_guard lock(mutex);
    progress = p;
}

void Job::add_log(const std::string& line) {
    if (line.empty()) return;
    std::lock_guard lock(mutex);
    log.push_back(line);
}

void prepare_start(Job& job, const std::string& dump) {
    job.running = true;
    job.finished = false;
    job.ok = false;
    job.cancel = false;
    job.error.clear();
    job.log.clear();
    std::thread([&job, dump] {
        bool ok = true;
        if (!dump.empty()) ok = import_dump(job, dump);
#ifdef _WIN32
        if (ok) ok = ensure_toolchain(job);
#endif
        if (ok && readiness() != Readiness::Ready) ok = build_game(job);
        if (job.cancel && job.error.empty()) job.error = "Cancelado.";
        job.ok = ok && !job.cancel;
        job.set_step(job.ok ? "Pronto!" : "Parou: " + job.error, job.ok ? 1 : 0);
        job.running = false;
        job.finished = true;
    }).detach();
}

}  // namespace launcher
