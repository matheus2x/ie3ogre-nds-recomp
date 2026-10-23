// Player settings: boej.ini next to the executable (NTR_CONFIG picks another file).
//
// An INI file of "key = value" lines under [section] headers; lines starting with ; or #
// are comments. The built-in defaults below are the same text the host writes on the
// first windowed run, and they are read first, so a file missing some keys (an older
// version, or keys deleted by hand) still gets the defaults for those.
#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>

#include <ntr/common.h>

namespace ntr {

namespace {

const char kDefaultConfig[] = R"(; Configurações do port de Inazuma Eleven 3 - The Ogre.
; Criado na primeira execução. Apague o arquivo para voltar ao padrão.
; As variáveis NTR_SCALE, NTR_3D_SCALE e NTR_MUTE, quando definidas, têm prioridade.

[video]
; Tela larga 16:9 (tamanho pela altura do monitor): as duas telas à esquerda e a de baixo
; ampliada à direita. false = só as duas telas empilhadas, centralizadas, com bordas pretas.
wide_layout = true
; Tamanho inicial da janela, em múltiplos de 256x384 (só com wide_layout = false).
window_scale = 2
; Resolução interna do 3D: 1 (nativa) a 4.
resolution_3d = 2
; Abrir em tela cheia (o atalho "fullscreen" alterna).
fullscreen = false
; Ampliação da imagem: nearest (pixels nítidos) ou linear (suavizada).
filter = nearest
; Ampliar só em múltiplos inteiros, com bordas pretas em volta (só com wide_layout = false).
integer_scale = false

[audio]
; Volume de 0 a 100 (0 = sem som).
volume = 100

; Opções do launcher (o jogo ignora esta seção).
[launcher]
; Procurar uma versão nova ao abrir o launcher.
check_updates = true
; Fechar o launcher quando o jogo abrir.
close_on_play = true

; Teclado: nomes de tecla do SDL (veja https://wiki.libsdl.org/SDL3/SDL_Keycode,
; sem o "SDLK_"), separados por vírgula. Deixe vazio para desligar.
; Botões do DS: A B X Y L R START SELECT UP DOWN LEFT RIGHT.
; Atalhos: menu (pausa, com as configurações), fast_forward (segurar) e fast_forward_toggle
; (avanço rápido), turbo (segurar) e turbo_toggle (A automático), screenshot, debug_dump
; (estado no boej.log) e fullscreen.
[keyboard]
A = X
B = Z
X = S
Y = A
L = Q
R = W
START = Return
SELECT = Right Shift, Backspace
UP = Up
DOWN = Down
LEFT = Left
RIGHT = Right
menu = Escape
fast_forward = Tab
fast_forward_toggle = F2
turbo = C
turbo_toggle = F3
screenshot = F12
debug_dump = F8
fullscreen = F11

; Controle: os mesmos botões e atalhos. Botões pela posição: south, east, west, north
; (A do DS = east, como num controle da Nintendo); também back, start, guide,
; leftshoulder, rightshoulder, leftstick, rightstick, dpup, dpdown, dpleft, dpright.
; Eixos: leftx, lefty, rightx, righty com + ou - (lefty- = analógico para cima),
; e lefttrigger, righttrigger.
[gamepad]
A = east
B = south
X = north
Y = west
L = leftshoulder
R = rightshoulder
START = start
SELECT = back
UP = dpup, lefty-
DOWN = dpdown, lefty+
LEFT = dpleft, leftx-
RIGHT = dpright, leftx+
menu = guide
fast_forward = righttrigger
fast_forward_toggle =
turbo = lefttrigger
turbo_toggle =
screenshot =
debug_dump =
fullscreen =
)";

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(uint8_t(s[a]))) a++;
    while (b > a && std::isspace(uint8_t(s[b - 1]))) b--;
    return s.substr(a, b - a);
}

std::string lower(std::string s) {
    for (char& ch : s) ch = char(std::tolower(uint8_t(ch)));
    return s;
}

void parse(std::istream& in, bool user, Config& out) {
    std::string line, section;
    int number = 0;
    while (std::getline(in, line)) {
        number++;
        line = trim(line);
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        if (line.front() == '[' && line.back() == ']') {
            section = lower(trim(line.substr(1, line.size() - 2)));
            continue;
        }
        size_t eq = line.find('=');
        if (eq == std::string::npos) {
            NTR_LOG("[config] %s:%d: expected \"key = value\"\n", out.path.c_str(), number);
            continue;
        }
        std::string key = lower(trim(line.substr(0, eq)));
        Config::Entry& e = out.entries[section + "." + key];
        e.value = trim(line.substr(eq + 1));
        e.line = user ? number : 0;
    }
}

const Config& defaults() {
    static const Config config = [] {
        Config c;
        std::istringstream text(kDefaultConfig);
        parse(text, false, c);
        return c;
    }();
    return config;
}

}  // namespace

bool config_load(const std::string& path, Config& out) {
    out = defaults();
    out.path = path;
    std::ifstream file(fs_path(path));
    if (!file) return false;
    parse(file, true, out);
    NTR_LOG("[config] settings from %s\n", path.c_str());
    return true;
}

bool config_write_default(const std::string& path) {
    FILE* f = open_file(fs_path(path), "wb");
    if (!f) return false;
    std::fwrite(kDefaultConfig, 1, sizeof kDefaultConfig - 1, f);
    std::fclose(f);
    NTR_LOG("[config] wrote the default settings to %s\n", path.c_str());
    return true;
}

const Config::Entry* Config::find(const std::string& section, const std::string& key) const {
    auto it = entries.find(lower(section) + "." + lower(key));
    return it == entries.end() ? nullptr : &it->second;
}

std::string Config::get(const std::string& section, const std::string& key) const {
    const Entry* e = find(section, key);
    return e ? e->value : "";
}

int Config::get_int(const std::string& section, const std::string& key, int min, int max) const {
    const Entry* e = find(section, key);
    int v = 0;
    if (!e || std::sscanf(e->value.c_str(), "%d", &v) != 1 || v < min || v > max) {
        if (!e || !e->line) return min;
        warn(*e, "\"" + e->value + "\" is not a number from " + std::to_string(min) + " to " + std::to_string(max));
        return defaults().get_int(section, key, min, max);
    }
    return v;
}

bool Config::get_bool(const std::string& section, const std::string& key) const {
    const Entry* e = find(section, key);
    if (!e) return false;
    std::string v = lower(e->value);
    if (v == "true" || v == "1" || v == "yes" || v == "sim") return true;
    if (v != "false" && v != "0" && v != "no" && v != "nao" && v != "não") warn(*e, "\"" + e->value + "\" is not true or false");
    return false;
}

void Config::set(const std::string& section, const std::string& key, const std::string& value) {
    Entry& e = entries[lower(section) + "." + lower(key)];
    e.value = value;
    if (!e.line) e.line = -1;  // changed by the launcher: no longer the default
}

bool config_save(const std::string& path, const Config& config) {
    std::istringstream defaults(kDefaultConfig);
    std::string out, line, section;
    while (std::getline(defaults, line)) {
        std::string t = trim(line);
        size_t eq = t.find('=');
        if (!t.empty() && t[0] == '[' && t.back() == ']') {
            section = lower(trim(t.substr(1, t.size() - 2)));
        } else if (!t.empty() && t[0] != ';' && t[0] != '#' && eq != std::string::npos) {
            std::string key = trim(t.substr(0, eq));
            std::string value = config.get(section, key);
            line = key + " =" + (value.empty() ? "" : " " + value);
        }
        out += line + "\n";
    }
    FILE* f = open_file(fs_path(path), "wb");
    if (!f) return false;
    bool ok = std::fwrite(out.data(), 1, out.size(), f) == out.size();
    ok = std::fclose(f) == 0 && ok;
    return ok;
}

void Config::warn(const Entry& e, const std::string& message) const {
    NTR_LOG("[config] %s:%d: %s\n", e.line ? path.c_str() : "defaults", e.line, message.c_str());
}

}  // namespace ntr
