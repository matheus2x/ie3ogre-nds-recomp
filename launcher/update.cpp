// Updates from GitHub Releases: the latest release's platform zip, checked against the
// release's sha256sums.txt, replaces the launcher and sdk/ in place. Player data (boej.pak,
// saves, boej.ini, the code modules) is never touched; the game is rebuilt afterwards
// because the build stamp names the SDK version.
//
// Release assets: ie3ogre-recomp-<X.Y.Z>-<platform>.zip (one folder, ie3ogre-recomp/, with
// the launcher, SDL3.dll and sdk/) and sha256sums.txt ("<hex>  <file name>" lines).
#include <SDL3/SDL.h>
#include <imgui.h>

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <thread>

#include "launcher.h"

namespace fs = std::filesystem;

namespace launcher {

namespace {

constexpr const char* kDefaultRepo = "matheus2x/ie3ogre-nds-recomp";
#ifdef _WIN32
constexpr const char* kPlatform = "windows";
#else
constexpr const char* kPlatform = "linux";
#endif

enum class State { Idle, Checking, UpToDate, Available, Failed, Downloading, Installed };

struct Update {
    std::mutex mutex;
    State state = State::Idle;
    std::string message;  // error or detail
    std::string version, page, zip_url, zip_name, sums_url;
    uint64_t zip_size = 0;
    std::atomic<double> progress{0};
};
Update g_update;

std::string repo() {
    const char* env = std::getenv("IE3_UPDATE_REPO");  // for testing against a fork
    return env ? env : kDefaultRepo;
}

// The latest-release endpoint; IE3_UPDATE_API replaces it in tests (a file:// URL works).
std::string api_url() {
    const char* env = std::getenv("IE3_UPDATE_API");
    return env ? env : "https://api.github.com/repos/" + repo() + "/releases/latest";
}

void set_state(State s, const std::string& message = "") {
    std::lock_guard lock(g_update.mutex);
    g_update.state = s;
    g_update.message = message;
}

// The JSON string value after "key": at or after `from`.
std::string json_string(const std::string& json, const std::string& key, size_t from = 0, size_t* at = nullptr) {
    size_t k = json.find("\"" + key + "\"", from);
    if (k == std::string::npos) return "";
    size_t colon = json.find(':', k);
    size_t q = json.find('"', colon);
    if (colon == std::string::npos || q == std::string::npos) return "";
    std::string out;
    size_t i = q + 1;
    for (; i < json.size() && json[i] != '"'; i++) {
        if (json[i] == '\\' && i + 1 < json.size()) i++;
        out += json[i];
    }
    if (at) *at = i;
    return out;
}

std::string strip_v(const std::string& tag) { return !tag.empty() && (tag[0] == 'v' || tag[0] == 'V') ? tag.substr(1) : tag; }

void check() {
    std::string out;
    int code = run_process({system_tool("curl"), "-L", "--fail", "--silent", "--show-error", "--max-time", "20",
                            "-H", "Accept: application/vnd.github+json", "-H", "User-Agent: ie3-launcher",
                            api_url()},
                           out);
    if (code != 0) {
        NTR_LOG("[update] check failed (%d): %s\n", code, out.c_str());
        set_state(State::Failed, "Não foi possível consultar as releases (sem internet, ou o repositório ainda não tem releases públicas).");
        return;
    }
    std::string version = strip_v(json_string(out, "tag_name"));
    std::string page = json_string(out, "html_url");
    std::string zip_name = "ie3ogre-recomp-" + version + "-" + kPlatform + ".zip";
    std::string zip_url, sums_url;
    uint64_t zip_size = 0;
    for (size_t pos = 0;;) {
        size_t at;
        std::string url = json_string(out, "browser_download_url", pos, &at);
        if (url.empty()) break;
        std::string name = url.substr(url.find_last_of('/') + 1);
        if (name == zip_name) {
            zip_url = url;
            // "size" comes before "browser_download_url" in each asset object.
            size_t s = out.rfind("\"size\"", at);
            if (s != std::string::npos) zip_size = std::strtoull(out.c_str() + out.find(':', s) + 1, nullptr, 10);
        }
        if (name == "sha256sums.txt") sums_url = url;
        pos = at;
    }
    NTR_LOG("[update] latest release %s (this is %s)\n", version.c_str(), LAUNCHER_VERSION);
    std::lock_guard lock(g_update.mutex);
    g_update.version = version;
    g_update.page = page;
    g_update.zip_url = zip_url;
    g_update.zip_name = zip_name;
    g_update.zip_size = zip_size;
    g_update.sums_url = sums_url;
    if (version.empty() || compare_versions(version, LAUNCHER_VERSION) <= 0) {
        g_update.state = State::UpToDate;
    } else if (zip_url.empty() || sums_url.empty()) {
        g_update.state = State::Failed;
        g_update.message = "A versão " + version + " não tem o pacote para " + kPlatform + " ou o sha256sums.txt.";
    } else {
        g_update.state = State::Available;
    }
}

bool download(const std::string& url, const std::string& path, uint64_t size, std::string& error) {
    std::atomic<bool> done{false};
    std::string out;
    int code = 0;
    std::thread curl([&] {
        code = run_process({system_tool("curl"), "-L", "--fail", "--silent", "--show-error", "--retry", "3", "-o", path, url}, out);
        done = true;
    });
    while (!done) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        std::error_code ec;
        uintmax_t now = fs::file_size(ntr::fs_path(path), ec);
        if (!ec && size) g_update.progress = double(now) / double(size);
    }
    curl.join();
    if (code != 0) error = out;
    return code == 0;
}

// Moves the new release over the old one. Files in use (the running launcher) cannot be
// overwritten on Windows but can be renamed, so each old file becomes *.old first and
// update_cleanup deletes them on the next start.
bool install(const fs::path& from, std::string& error) {
    fs::path root = ntr::fs_path(paths().root);
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(from)) {
        fs::path target = root / entry.path().filename();
        fs::path old = target;
        old += ".old";
        fs::remove_all(old, ec);
        if (fs::exists(target)) fs::rename(target, old, ec);
        if (ec) {
            error = "não foi possível substituir " + ntr::utf8_string(target) + ": " + ec.message();
            return false;
        }
        fs::rename(entry.path(), target, ec);
        if (ec) {
            fs::rename(old, target, ec);
            error = "não foi possível instalar " + ntr::utf8_string(target);
            return false;
        }
    }
    return true;
}

void download_and_install() {
    std::string version, zip_url, zip_name, sums_url;
    uint64_t zip_size;
    {
        std::lock_guard lock(g_update.mutex);
        version = g_update.version;
        zip_url = g_update.zip_url;
        zip_name = g_update.zip_name;
        sums_url = g_update.sums_url;
        zip_size = g_update.zip_size;
    }
    std::string dir = paths().cache() + "update/";
    std::error_code ec;
    fs::remove_all(ntr::fs_path(dir), ec);
    fs::create_directories(ntr::fs_path(dir), ec);
    std::string error, zip = dir + zip_name, sums = dir + "sha256sums.txt";
    g_update.progress = 0;
    if (!download(sums_url, sums, 0, error) || !download(zip_url, zip, zip_size, error)) {
        set_state(State::Failed, "O download falhou: " + error);
        return;
    }
    std::string expected;
    std::ifstream in(ntr::fs_path(sums));
    for (std::string line; std::getline(in, line);) {
        std::istringstream fields(line);
        std::string hex, name;
        fields >> hex >> name;
        if (!name.empty() && (name[0] == '*')) name = name.substr(1);
        if (name.rfind("./", 0) == 0) name = name.substr(2);
        if (name == zip_name) expected = hex;
    }
    std::string actual = ntr::sha256_file(zip);
    if (expected.empty() || actual != expected) {
        NTR_LOG("[update] %s: SHA-256 %s, expected %s\n", zip_name.c_str(), actual.c_str(), expected.c_str());
        set_state(State::Failed, "O pacote baixado não confere com o sha256sums.txt. Nada foi alterado.");
        return;
    }
    std::string out;
#ifdef _WIN32
    int code = run_process({system_tool("tar"), "-xf", zip, "-C", dir}, out);
#else
    int code = run_process({"unzip", "-q", "-o", zip, "-d", dir}, out);
#endif
    fs::path unpacked = ntr::fs_path(dir) / "ie3ogre-recomp";
    if (code != 0 || !fs::is_directory(unpacked)) {
        set_state(State::Failed, "Não foi possível extrair o pacote: " + out);
        return;
    }
    if (!install(unpacked, error)) {
        set_state(State::Failed, "A instalação falhou (" + error + ").");
        return;
    }
    fs::remove_all(ntr::fs_path(dir), ec);
    NTR_LOG("[update] installed %s\n", version.c_str());
    set_state(State::Installed);
}

}  // namespace

void update_cleanup() {
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(ntr::fs_path(paths().root), ec)) {
        if (entry.path().extension() == ".old") fs::remove_all(entry.path(), ec);
    }
    fs::remove_all(ntr::fs_path(paths().cache() + "update"), ec);
}

void update_check_start() {
    {
        std::lock_guard lock(g_update.mutex);
        if (g_update.state == State::Checking || g_update.state == State::Downloading) return;
        g_update.state = State::Checking;
    }
    std::thread(check).detach();
}

int update_cli() {
    set_state(State::Checking);
    check();
    std::string message, version;
    State state;
    {
        std::lock_guard lock(g_update.mutex);
        state = g_update.state;
        message = g_update.message;
        version = g_update.version;
    }
    if (state == State::UpToDate) {
        NTR_LOG("[update] up to date (%s)\n", LAUNCHER_VERSION);
        return 0;
    }
    if (state != State::Available) {
        NTR_LOG("error: %s\n", message.c_str());
        return 1;
    }
    NTR_LOG("[update] installing %s\n", version.c_str());
    download_and_install();
    std::lock_guard lock(g_update.mutex);
    if (g_update.state != State::Installed) NTR_LOG("error: %s\n", g_update.message.c_str());
    return g_update.state == State::Installed ? 0 : 1;
}

bool update_available() {
    std::lock_guard lock(g_update.mutex);
    return g_update.state == State::Available;
}

void update_ui() {
    ImGui::Text("Versão instalada: %s", LAUNCHER_VERSION);
    ImGui::TextDisabled("Releases: github.com/%s", repo().c_str());
    ImGui::Spacing();
    std::unique_lock lock(g_update.mutex);
    State state = g_update.state;
    switch (state) {
        case State::Idle:
            break;
        case State::Checking:
            ImGui::TextUnformatted("Procurando...");
            break;
        case State::UpToDate:
            ImGui::TextUnformatted("Você está na versão mais recente.");
            break;
        case State::Failed:
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0.45f, 0.4f, 1));
            ImGui::TextWrapped("%s", g_update.message.c_str());
            ImGui::PopStyleColor();
            break;
        case State::Available:
            ImGui::Text("Versão nova disponível: %s (%s)", g_update.version.c_str(), format_bytes(g_update.zip_size).c_str());
            if (ImGui::Button("Baixar e instalar")) {
                g_update.state = State::Downloading;
                std::thread(download_and_install).detach();
            }
            ImGui::SameLine();
            if (ImGui::Button("Ver as novidades")) SDL_OpenURL(g_update.page.c_str());
            ImGui::TextDisabled("Seus dados (boej.pak, saves, configurações) ficam como estão.");
            break;
        case State::Downloading:
            ImGui::Text("Baixando a versão %s...", g_update.version.c_str());
            ImGui::ProgressBar(float(g_update.progress.load()));
            break;
        case State::Installed:
            ImGui::Text("A versão %s foi instalada.", g_update.version.c_str());
            ImGui::TextWrapped("Reinicie o launcher. O jogo será compilado de novo para a versão nova (o dump não é necessário).");
            if (ImGui::Button("Reiniciar o launcher")) {
                lock.unlock();
                start_process({paths().launcher()}, paths().root);
                std::exit(0);
            }
            break;
    }
    if (state != State::Checking && state != State::Downloading && state != State::Installed) {
        if (lock.owns_lock()) lock.unlock();
        if (ImGui::Button("Procurar atualizações")) update_check_start();
    }
}

}  // namespace launcher
