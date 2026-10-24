// Host frontend on SDL3: one window with both screens, keyboard/gamepad/mouse input and
// frame pacing to the DS refresh rate (59.8261 Hz).
//
// Two layouts (video.wide_layout): wide (the default) is a 16:9 window sized from the
// monitor's height, with both screens stacked in a side column (left by default,
// video.wide_side) and the bottom screen enlarged beside it; otherwise the screens are stacked and centered, with black bars.
//
// Settings, key and gamepad bindings come from boej.ini (config.cpp), which also lists
// the defaults: arrows = D-pad, X = A, Z = B, S = X, A = Y, Q = L, W = R, Enter = Start,
// Right Shift/Backspace = Select, F12 = screenshot, F8 = state dump to the log (plus a
// screenshot), F11 = fullscreen. The left mouse button touches the bottom screen.
//
// Testing shortcuts: Tab (held) or F2 (toggle) = fast forward; C (held) or F3 (toggle) =
// auto A, which presses and releases A every few frames to skip through messages. On a
// gamepad, the right trigger fast-forwards and the left trigger is auto A. The window
// title shows which ones are on.
//
// Without a window the settings file is read only if NTR_CONFIG names it, so tests do
// not depend on the player's settings.
//
// Environment: NTR_HEADLESS (no window, unthrottled), NTR_FAST (unthrottled), NTR_MUTE (no
// audio device), NTR_WAV (also write the sound output to that WAV file), NTR_CONFIG
// (settings file instead of boej.ini next to the executable), NTR_SCALE (initial window
// scale), NTR_3D_SCALE (3D internal resolution, 1-10; 1 without a window),
// NTR_SCREENSHOT_FRAMES + NTR_SCREENSHOT
// (save those frames, a comma-separated list, as BMPs of what the window shows; %d in the
// path is the frame), NTR_EXIT_FRAME (exit after that many frames),
// NTR_INPUT (scripted input for tests: "first-last:KEY[+KEY...];..." with frame numbers
// and keys A B X Y L R START SELECT UP DOWN LEFT RIGHT or TOUCH@x,y), NTR_PEEK
// ("hexaddr:size", dumps guest memory every frame), NTR_REPLAY (replays a session
// recording, see diag.cpp), NTR_RECORD (record the session even when headless or scripted),
// NTR_STATE_SAVE / NTR_STATE_LOAD (save to / load from savestate slot 1 at that frame).
//
// Savestates (savestate.cpp): F5 saves to the chosen slot, F9 loads it, F6/F7 choose the
// slot (1-5); the pause menu has a page for them too.
#define SDL_MAIN_HANDLED  // main() is ours; SDL_SetMainReady() is called instead
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <ntr/runtime.h>
#include <ntr/ui.h>

#include "runtime.h"

namespace ntr {

namespace {

constexpr uint64_t kFrameNs = 1000000000ull * 263 * 2130 / 33513982;  // 355x263 dots, 6 cycles each

// What a key or gamepad input does: a DS button (KEYINPUT bits 0-9, X = bit 10, Y = bit 11)
// or a frontend shortcut.
enum Action {
    kFastForward = 12,
    kFastForwardToggle,
    kTurbo,
    kTurboToggle,
    kScreenshot,
    kDebugDump,
    kFullscreen,
    kSaveState,
    kLoadState,
    kStateSlotPrev,
    kStateSlotNext,
    kMenu,
    kActionCount
};
constexpr uint32_t kButtonMask = 0xFFF;

// The wide layout in logical pixels: 16:9, both screens stacked in one third (the left
// one, or the right with video.wide_side = right) and the bottom screen in the other two
// thirds, stretched to the full height like melonDS's hybrid layout (no black bars, so
// the pixels are 12.5% taller than on a DS).
constexpr int kWideWidth = 2304, kWideHeight = 1296;
constexpr float kWideStackWidth = 768;

const char* const kActionNames[kActionCount] = {
    "A", "B", "SELECT", "START", "RIGHT", "LEFT", "UP", "DOWN", "R", "L", "X", "Y",
    "fast_forward", "fast_forward_toggle", "turbo", "turbo_toggle", "screenshot", "debug_dump", "fullscreen",
    "save_state", "load_state", "state_slot_prev", "state_slot_next", "menu"};

struct KeyBinding {
    SDL_Keycode key;
    int action;
};

// A gamepad button, or an axis pushed past half way (dir = +1 or -1).
struct PadBinding {
    int button = SDL_GAMEPAD_BUTTON_INVALID;
    int axis = SDL_GAMEPAD_AXIS_INVALID;
    int dir = 0;
    int action;
};

struct ScriptedInput {
    uint64_t first, last;
    uint16_t keys;
    bool touch;
    int x, y;
};

struct Host {
    bool headless = true;
    bool throttle = true;
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_Texture* textures[2] = {};  // top and bottom screen
    int texture_scales[2] = {};
    uint64_t frame = 0;
    uint64_t deadline = 0;
    uint64_t fps_start = 0, fps_frames = 0;
    // Frames whose work (emulation, rendering, presenting) took longer than a DS frame.
    uint64_t work_start = 0, worst_ns = 0, worst_frame = 0;
    uint32_t slow_frames = 0;
    // Without a window: frame times for the whole run, logged at exit (benchmarks).
    uint64_t run_last = 0, run_ns = 0, run_frames = 0, run_worst_ns = 0, run_worst_frame = 0, run_slow = 0;
    std::set<uint64_t> screenshot_frames;
    std::string screenshot_path = "boej-%d.bmp";  // %d = frame number
    uint64_t f12_frame = 0, exit_frame = 0;

    Config config;
    std::vector<KeyBinding> key_bindings;
    std::vector<PadBinding> pad_bindings;
    std::set<SDL_Keycode> keys_down;
    std::vector<SDL_Gamepad*> pads;
    uint32_t key_actions = 0, pad_actions = 0, actions = 0;  // held, by Action bit

    uint16_t keys = 0;     // pressed DS buttons: KEYINPUT bits 0-9, X = bit 10, Y = bit 11
    bool touching = false;
    int touch_x = 0, touch_y = 0;
    bool ff_held = false, ff_toggle = false;        // fast forward
    bool turbo_held = false, turbo_toggle = false;  // auto A
    int ff_speed = 4;                               // game.fast_forward_speed
    bool linear_filter = false;
    std::string config_path;
    bool env_scale = false, env_scale_3d = false;  // NTR_SCALE / NTR_3D_SCALE win over boej.ini
    bool fullscreen_applied = false;
    int window_scale_applied = 0;
    bool wide = true;          // video.wide_layout
    bool stack_right = false;  // video.wide_side = right
    bool menu_requested = false;
    bool ui_ready = false;
    int state_slot = 1;  // the savestate slot the shortcuts use
    uint64_t state_save_frame = 0, state_load_frame = 0;  // NTR_STATE_SAVE / NTR_STATE_LOAD
    std::string toast;      // a short message over the picture (ASCII: SDL's debug font)
    uint64_t toast_until = 0;
    std::vector<std::string> args;  // for restarting
    std::vector<ScriptedInput> script;
    uint16_t script_keys = 0;
    bool script_touch = false;
    uint32_t peek_addr = 0, peek_size = 0;

    SDL_AudioStream* audio = nullptr;
    FILE* wav = nullptr;  // NTR_WAV: the sound output, for checks without a window
    uint32_t wav_frames = 0;
};
Host g_host;

uint64_t env_u64(const char* name) {
    const char* v = std::getenv(name);
    return v ? std::strtoull(v, nullptr, 10) : 0;
}

// Both screens at `scale`; a screen given at a smaller scale (top_scale, bottom_scale) is
// enlarged by repeating its pixels.
void write_bmp(const std::string& path, const uint32_t* top, int top_scale, const uint32_t* bottom, int bottom_scale,
               int scale) {
    FILE* f = open_file(fs_path(path), "wb");
    if (!f) {
        NTR_LOG("[host] cannot write %s\n", path.c_str());
        return;
    }
    const uint32_t screen_h = kScreenHeight * scale;
    const uint32_t w = kScreenWidth * scale, h = screen_h * 2, row = w * 3, size = 54 + row * h;
    uint8_t hdr[54] = {'B', 'M'};
    auto put32 = [&](int at, uint32_t v) { store32(hdr + at, v); };
    put32(2, size);
    put32(10, 54);
    put32(14, 40);
    put32(18, w);
    put32(22, h);
    hdr[26] = 1;
    hdr[28] = 24;
    put32(34, row * h);
    std::fwrite(hdr, 1, sizeof hdr, f);
    for (int y = int(h) - 1; y >= 0; y--) {  // bottom-up rows
        bool upper = uint32_t(y) < screen_h;
        int from = upper ? top_scale : bottom_scale;
        uint32_t sy = (upper ? y : y - screen_h) * from / scale;
        const uint32_t* src = (upper ? top : bottom) + sy * kScreenWidth * from;
        for (uint32_t x = 0; x < w; x++) {
            uint32_t c = src[x * from / scale];
            uint8_t bgr[3] = {uint8_t(c), uint8_t(c >> 8), uint8_t(c >> 16)};
            std::fwrite(bgr, 1, 3, f);
        }
    }
    std::fclose(f);
    NTR_LOG("[host] frame %llu saved to %s\n", (unsigned long long)g_host.frame, path.c_str());
}

uint16_t key_by_name(const std::string& name) {
    for (int i = 0; i < kFastForward; i++) {
        if (name == kActionNames[i]) return uint16_t(1u << i);
    }
    NTR_LOG("[host] NTR_INPUT: unknown key %s\n", name.c_str());
    return 0;
}

void parse_script(const std::string& text) {
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find(';', pos);
        std::string item = text.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
        pos = end == std::string::npos ? text.size() : end + 1;
        ScriptedInput in{};
        unsigned long long first = 0, last = 0;
        char keys[128] = {};
        if (std::sscanf(item.c_str(), "%llu-%llu:%127s", &first, &last, keys) != 3) {
            NTR_LOG("[host] NTR_INPUT: cannot parse \"%s\"\n", item.c_str());
            continue;
        }
        in.first = first;
        in.last = last;
        std::string list = keys;
        size_t k = 0;
        while (k < list.size()) {
            size_t plus = list.find('+', k);
            std::string key = list.substr(k, plus == std::string::npos ? std::string::npos : plus - k);
            k = plus == std::string::npos ? list.size() : plus + 1;
            if (std::sscanf(key.c_str(), "TOUCH@%d,%d", &in.x, &in.y) == 2) in.touch = true;
            else in.keys |= key_by_name(key);
        }
        g_host.script.push_back(in);
    }
}

// A session recording: NTR_INPUT items one per line, "# rtc <start>" for the clock.
void load_replay(const char* path) {
    FILE* f = std::fopen(path, "r");
    if (!f) {
        NTR_LOG("[host] cannot read the recording %s\n", path);
        return;
    }
    std::string items;
    char line[256];
    while (std::fgets(line, sizeof line, f)) {
        std::string l = line;
        while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back();
        if (l.rfind("# rtc ", 0) == 0) rtc_request_start(l.substr(6));
        if (l.empty() || l[0] == '#') continue;
        items += l + ";";
    }
    std::fclose(f);
    parse_script(items);
    NTR_LOG("[host] replaying %s (%zu input ranges)\n", path, g_host.script.size());
}

void apply_script() {
    g_host.script_keys = 0;
    g_host.script_touch = false;
    for (const ScriptedInput& in : g_host.script) {
        if (g_host.frame < in.first || g_host.frame > in.last) continue;
        g_host.script_keys |= in.keys;
        if (in.touch) {
            g_host.script_touch = true;
            g_host.touch_x = in.x;
            g_host.touch_y = in.y;
        }
    }
}

bool fast_forward() { return g_host.ff_held || g_host.ff_toggle; }

bool turbo() { return g_host.turbo_held || g_host.turbo_toggle; }

// Auto A: 3 frames pressed, 3 released, so the game sees a new press each time.
uint16_t turbo_keys() { return turbo() && (g_host.frame / 3) % 2 == 0 ? 1u << 0 : 0; }

uint16_t pressed_keys() { return g_host.keys | g_host.script_keys | turbo_keys(); }

// Replaces %d in a screenshot path with the current frame number.
std::string frame_path(std::string pattern) {
    size_t at = pattern.find("%d");
    if (at != std::string::npos) pattern.replace(at, 2, std::to_string(g_host.frame));
    return pattern;
}

void write_wav_header(FILE* f, uint32_t frames) {
    uint8_t h[44] = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, 2, 0};
    uint32_t data = frames * 4;
    store32(h + 4, 36 + data);
    store32(h + 24, kSoundRate);
    store32(h + 28, kSoundRate * 4);
    h[32] = 4;
    h[34] = 16;
    std::memcpy(h + 36, "data", 4);
    store32(h + 40, data);
    std::fseek(f, 0, SEEK_SET);
    std::fwrite(h, 1, sizeof h, f);
}

void quit() {
    if (g_host.headless && g_host.run_frames) {
        NTR_LOG("[perf] %llu frames: %.2f ms on average, worst %.1f ms (frame %llu), %llu longer than 16.7 ms\n",
                (unsigned long long)g_host.run_frames, g_host.run_ns / 1e6 / g_host.run_frames,
                g_host.run_worst_ns / 1e6, (unsigned long long)g_host.run_worst_frame,
                (unsigned long long)g_host.run_slow);
    }
    if (g_host.wav) {
        write_wav_header(g_host.wav, g_host.wav_frames);
        std::fclose(g_host.wav);
    }
    replay_finish(g_host.frame);
    NTR_LOG("[host] exit at frame %llu\n", (unsigned long long)g_host.frame);
    if (!g_host.headless) SDL_Quit();
    std::exit(0);
}

void open_audio() {
    if (std::getenv("NTR_MUTE")) return;
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        NTR_LOG("[host] no audio: %s\n", SDL_GetError());
        return;
    }
    SDL_AudioSpec spec{SDL_AUDIO_S16, 2, kSoundRate};
    g_host.audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (!g_host.audio) {
        NTR_LOG("[host] no audio: %s\n", SDL_GetError());
        return;
    }
    int volume = g_host.config.get_int("audio", "volume", 0, 100);
    SDL_SetAudioStreamGain(g_host.audio, volume / 100.0f);
    SDL_ResumeAudioStreamDevice(g_host.audio);
}

// "Right Shift, Backspace" -> {"Right Shift", "Backspace"}
std::vector<std::string> split_list(const std::string& value) {
    std::vector<std::string> items;
    size_t pos = 0;
    while (pos <= value.size()) {
        size_t comma = value.find(',', pos);
        std::string item = value.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
        size_t a = item.find_first_not_of(" \t"), b = item.find_last_not_of(" \t");
        if (a != std::string::npos) items.push_back(item.substr(a, b - a + 1));
        if (comma == std::string::npos) break;
        pos = comma + 1;
    }
    return items;
}

// Positions first, so that "east" means the same button on any gamepad; then SDL's
// mapping names ("a" = south, "b" = east, "back", "dpup"...).
int pad_button_by_name(const std::string& name) {
    static const std::pair<const char*, int> kPositions[] = {
        {"south", SDL_GAMEPAD_BUTTON_SOUTH}, {"east", SDL_GAMEPAD_BUTTON_EAST},
        {"west", SDL_GAMEPAD_BUTTON_WEST}, {"north", SDL_GAMEPAD_BUTTON_NORTH}};
    for (const auto& [n, button] : kPositions) {
        if (SDL_strcasecmp(name.c_str(), n) == 0) return button;
    }
    return SDL_GetGamepadButtonFromString(name.c_str());
}

void load_bindings() {
    const Config& c = g_host.config;
    g_host.key_bindings.clear();
    g_host.pad_bindings.clear();
    for (int action = 0; action < kActionCount; action++) {
        if (const Config::Entry* e = c.find("keyboard", kActionNames[action])) {
            for (const std::string& name : split_list(e->value)) {
                SDL_Keycode key = SDL_GetKeyFromName(name.c_str());
                if (key == SDLK_UNKNOWN) c.warn(*e, "unknown key \"" + name + "\"");
                else g_host.key_bindings.push_back({key, action});
            }
        }
        if (const Config::Entry* e = c.find("gamepad", kActionNames[action])) {
            for (std::string name : split_list(e->value)) {
                PadBinding b;
                b.action = action;
                char sign = name.back();
                if (sign == '+' || sign == '-') name.pop_back();
                if ((b.button = pad_button_by_name(name)) == SDL_GAMEPAD_BUTTON_INVALID) {
                    b.axis = SDL_GetGamepadAxisFromString(name.c_str());
                    b.dir = sign == '-' ? -1 : 1;
                }
                if (b.button == SDL_GAMEPAD_BUTTON_INVALID && b.axis == SDL_GAMEPAD_AXIS_INVALID) {
                    c.warn(*e, "unknown gamepad button or axis \"" + name + "\"");
                } else {
                    g_host.pad_bindings.push_back(b);
                }
            }
        }
    }
}

void toggle_fullscreen() {
    bool on = SDL_GetWindowFlags(g_host.window) & SDL_WINDOW_FULLSCREEN;
    SDL_SetWindowFullscreen(g_host.window, !on);
}

void show_toast(const std::string& text) {
    g_host.toast = text;
    g_host.toast_until = SDL_GetTicksNS() + 2500000000ull;
}

// The slot the shortcuts now use, and what it holds when that is a date.
void change_slot(int delta) {
    g_host.state_slot = (g_host.state_slot - 1 + delta + kStateSlots) % kStateSlots + 1;
    std::string text = "Slot " + std::to_string(g_host.state_slot);
    std::string held = savestate_describe(g_host.state_slot);
    if (held.empty()) text += " (vazio)";
    else if (std::isdigit(uint8_t(held[0]))) text += ": " + held;
    show_toast(text);
}

// Recomputes what is held from the keyboard and the gamepads, and runs the shortcuts
// that were just pressed.
void update_actions() {
    g_host.key_actions = 0;
    for (const KeyBinding& b : g_host.key_bindings) {
        if (g_host.keys_down.count(b.key)) g_host.key_actions |= 1u << b.action;
    }
    g_host.pad_actions = 0;
    for (SDL_Gamepad* pad : g_host.pads) {
        for (const PadBinding& b : g_host.pad_bindings) {
            bool on = b.button != SDL_GAMEPAD_BUTTON_INVALID
                          ? SDL_GetGamepadButton(pad, SDL_GamepadButton(b.button))
                          : SDL_GetGamepadAxis(pad, SDL_GamepadAxis(b.axis)) * b.dir > 16000;
            if (on) g_host.pad_actions |= 1u << b.action;
        }
    }
    uint32_t now = g_host.key_actions | g_host.pad_actions;
    uint32_t pressed = now & ~g_host.actions;
    g_host.actions = now;
    g_host.keys = uint16_t(now & kButtonMask);
    g_host.ff_held = now & (1u << kFastForward);
    g_host.turbo_held = now & (1u << kTurbo);
    if (pressed & (1u << kFastForwardToggle)) g_host.ff_toggle = !g_host.ff_toggle;
    if (pressed & (1u << kTurboToggle)) g_host.turbo_toggle = !g_host.turbo_toggle;
    if (pressed & (1u << kScreenshot)) g_host.f12_frame = g_host.frame + 1;
    if (pressed & (1u << kDebugDump)) {
        debug_dump("F8");
        g_host.f12_frame = g_host.frame + 1;
    }
    if (pressed & (1u << kFullscreen)) toggle_fullscreen();
    if (pressed & (1u << kSaveState)) savestate_request_save(g_host.state_slot);
    if (pressed & (1u << kLoadState)) savestate_request_load(g_host.state_slot);
    if (pressed & (1u << kStateSlotPrev)) change_slot(-1);
    if (pressed & (1u << kStateSlotNext)) change_slot(1);
    if (pressed & (1u << kMenu)) g_host.menu_requested = true;
}

// The wide layout's column with both screens, and the enlarged bottom screen beside it.
SDL_FRect wide_stack() {
    return {g_host.stack_right ? kWideWidth - kWideStackWidth : 0, 0, kWideStackWidth, kWideHeight};
}
SDL_FRect wide_bottom() {
    return {g_host.stack_right ? 0 : kWideStackWidth, 0, kWideWidth - kWideStackWidth, kWideHeight};
}

// Window position -> bottom screen pixel; false outside the bottom screen.
bool to_touch(float wx, float wy, int& x, int& y) {
    float lx, ly;
    if (!SDL_RenderCoordinatesFromWindow(g_host.renderer, wx, wy, &lx, &ly)) return false;
    if (g_host.wide) {  // either copy of the bottom screen
        SDL_FRect big = wide_bottom(), stack = wide_stack();
        if (lx >= big.x && lx < big.x + big.w && ly >= big.y && ly < big.y + big.h) {
            x = int((lx - big.x) * kScreenWidth / big.w);
            y = int((ly - big.y) * kScreenHeight / big.h);
            return true;
        }
        lx = (lx - stack.x) * kScreenWidth / stack.w;
        ly = (ly - stack.y) * 2 * kScreenHeight / stack.h;
    }
    if (lx < 0 || lx >= kScreenWidth || ly < kScreenHeight || ly >= 2 * kScreenHeight) return false;
    x = int(lx);
    y = int(ly) - kScreenHeight;
    return true;
}

// A gamepad plugged in or out.
void pad_connection(const SDL_Event& ev) {
    if (ev.type == SDL_EVENT_GAMEPAD_ADDED) {
        if (SDL_Gamepad* pad = SDL_OpenGamepad(ev.gdevice.which)) {
            g_host.pads.push_back(pad);
            NTR_LOG("[host] gamepad: %s\n", SDL_GetGamepadName(pad));
        }
        return;
    }
    for (size_t i = 0; i < g_host.pads.size(); i++) {
        if (SDL_GetGamepadID(g_host.pads[i]) == ev.gdevice.which) {
            SDL_CloseGamepad(g_host.pads[i]);
            g_host.pads.erase(g_host.pads.begin() + i);
            break;
        }
    }
}

void pump_events() {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
            case SDL_EVENT_QUIT:
                quit();
                break;
            case SDL_EVENT_KEY_DOWN:
                g_host.keys_down.insert(ev.key.key);
                update_actions();
                break;
            case SDL_EVENT_KEY_UP:
                g_host.keys_down.erase(ev.key.key);
                update_actions();
                break;
            case SDL_EVENT_WINDOW_FOCUS_LOST:  // the key-up events go to another window
                g_host.keys_down.clear();
                update_actions();
                break;
            case SDL_EVENT_GAMEPAD_ADDED:
            case SDL_EVENT_GAMEPAD_REMOVED:
                pad_connection(ev);
                update_actions();
                break;
            case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
            case SDL_EVENT_GAMEPAD_BUTTON_UP:
            case SDL_EVENT_GAMEPAD_AXIS_MOTION:
                update_actions();
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (ev.button.button == SDL_BUTTON_LEFT) {
                    g_host.touching = to_touch(ev.button.x, ev.button.y, g_host.touch_x, g_host.touch_y);
                }
                break;
            case SDL_EVENT_MOUSE_BUTTON_UP:
                if (ev.button.button == SDL_BUTTON_LEFT) g_host.touching = false;
                break;
            case SDL_EVENT_MOUSE_MOTION:
                if (g_host.touching) {
                    int x, y;
                    if (to_touch(ev.motion.x, ev.motion.y, x, y)) {
                        g_host.touch_x = x;
                        g_host.touch_y = y;
                    }
                }
                break;
            default:
                break;
        }
    }
}

void pace() {
    uint64_t now = SDL_GetTicksNS();
    if (g_host.work_start && g_host.throttle && !fast_forward() && now - g_host.work_start > kFrameNs) {
        g_host.slow_frames++;
        if (now - g_host.work_start > g_host.worst_ns) {
            g_host.worst_ns = now - g_host.work_start;
            g_host.worst_frame = g_host.frame;
        }
    }
    if (!g_host.throttle) {
        g_host.deadline = now;
    } else {
        g_host.deadline += fast_forward() ? kFrameNs / g_host.ff_speed : kFrameNs;
        if (g_host.deadline > now) SDL_DelayPrecise(g_host.deadline - now);
        else if (now - g_host.deadline > 4 * kFrameNs) g_host.deadline = now;  // too far behind: resync
    }
    g_host.fps_frames++;
    if (now - g_host.fps_start >= 1000000000ull) {
        double fps = g_host.fps_frames * 1e9 / double(now - g_host.fps_start);
        char title[128];
        std::snprintf(title, sizeof title, "Inazuma Eleven 3 - The Ogre (recomp) - %.1f fps%s%s", fps,
                      fast_forward() ? " [>> acelerado]" : "",
                      turbo() ? " [A automatico]" : "");
        SDL_SetWindowTitle(g_host.window, title);
        g_host.fps_start = now;
        g_host.fps_frames = 0;
        // Late frames make the sound run dry (a stutter); the log says where it happened.
        if (g_host.slow_frames) {
            NTR_LOG("[perf] %u frames took longer than 16.7 ms in the last second (worst %.1f ms, frame %llu)\n",
                    g_host.slow_frames, g_host.worst_ns / 1e6, (unsigned long long)g_host.worst_frame);
            g_host.slow_frames = 0;
            g_host.worst_ns = 0;
        }
    }
    g_host.work_start = SDL_GetTicksNS();
}

// --- settings applied while the game runs ---------------------------------------------

// The logical size of the layout; the window shows it letterboxed.
void layout_size(int& w, int& h) {
    w = g_host.wide ? kWideWidth : kScreenWidth;
    h = g_host.wide ? kWideHeight : kScreenHeight * 2;
}

// Draws the screen textures with the layout's logical rectangle at (x, y) scaled by s.
void draw_screens(float x, float y, float s) {
    auto draw = [&](int screen, const SDL_FRect& rect) {
        SDL_FRect dst = {x + rect.x * s, y + rect.y * s, rect.w * s, rect.h * s};
        SDL_RenderTexture(g_host.renderer, g_host.textures[screen], nullptr, &dst);
    };
    if (!g_host.wide) {
        draw(0, {0, 0, kScreenWidth, kScreenHeight});
        draw(1, {0, kScreenHeight, kScreenWidth, kScreenHeight});
        return;
    }
    SDL_FRect stack = wide_stack();
    draw(0, {stack.x, stack.y, stack.w, stack.h / 2});
    draw(1, {stack.x, stack.y + stack.h / 2, stack.w, stack.h / 2});
    draw(1, wide_bottom());
}

// The message of the last savestate action or slot change, in the top corner on the
// side of the small screens (the left one in the stacked layout).
void draw_toast() {
    if (g_host.toast.empty()) return;
    if (SDL_GetTicksNS() > g_host.toast_until) {
        g_host.toast.clear();
        return;
    }
    SDL_Renderer* r = g_host.renderer;
    int lw, lh;
    layout_size(lw, lh);
    float s = std::max(1.0f, std::floor(lh / 320.0f));  // the 8-pixel font, about 1/40 of the height
    SDL_SetRenderScale(r, s, s);
    SDL_FRect box = {4, 4, 8.0f * float(g_host.toast.size()) + 8, 16};
    if (g_host.wide && g_host.stack_right) box.x = lw / s - box.w - 4;
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 190);
    SDL_RenderFillRect(r, &box);
    SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
    SDL_RenderDebugText(r, box.x + 4, 8, g_host.toast.c_str());
    SDL_SetRenderScale(r, 1, 1);
}

// A window size for the layout: wide is 16:9 at most 85% of the monitor's usable
// height; stacked is window_scale times 256x384.
void default_window_size(int scale, int& w, int& h) {
    if (!g_host.wide) {
        w = kScreenWidth * scale;
        h = kScreenHeight * 2 * scale;
        return;
    }
    SDL_Rect usable = {0, 0, 1280, 800};
    SDL_DisplayID display = g_host.window ? SDL_GetDisplayForWindow(g_host.window) : SDL_GetPrimaryDisplay();
    if (!SDL_GetDisplayUsableBounds(display, &usable)) NTR_LOG("[host] no display bounds: %s\n", SDL_GetError());
    h = std::max(usable.h * 85 / 100, kScreenHeight * 2);
    w = h * 16 / 9;
    if (w > usable.w * 95 / 100) {
        w = std::max(usable.w * 95 / 100, kScreenWidth * 2);
        h = w * 9 / 16;
    }
}

void apply_presentation() {
    const Config& c = g_host.config;
    g_host.wide = c.get_bool("video", "wide_layout");
    g_host.stack_right = SDL_strcasecmp(c.get("video", "wide_side").c_str(), "right") == 0;
    int w, h;
    layout_size(w, h);
    // Integer scaling of the 2304x1296 wide layout would not fit most windows.
    bool integer = c.get_bool("video", "integer_scale") && !g_host.wide;
    SDL_SetRenderLogicalPresentation(g_host.renderer, w, h,
                                     integer ? SDL_LOGICAL_PRESENTATION_INTEGER_SCALE
                                             : SDL_LOGICAL_PRESENTATION_LETTERBOX);
    g_host.linear_filter = SDL_strcasecmp(c.get("video", "filter").c_str(), "linear") == 0;
    for (SDL_Texture* t : g_host.textures) {
        if (t) SDL_SetTextureScaleMode(t, g_host.linear_filter ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);
    }
}

// After a change in the pause menu: everything boej.ini holds takes effect at once.
// Fullscreen and the window size only change when their own setting changed, so F11 and
// a window resized by hand are left alone otherwise.
void apply_settings() {
    const Config& c = g_host.config;
    if (g_host.audio) SDL_SetAudioStreamGain(g_host.audio, c.get_int("audio", "volume", 0, 100) / 100.0f);
    g_host.ff_speed = c.get_int("game", "fast_forward_speed", 2, 10);
    bool was_wide = g_host.wide;
    apply_presentation();
    bool fullscreen = c.get_bool("video", "fullscreen");
    if (fullscreen != g_host.fullscreen_applied) {
        SDL_SetWindowFullscreen(g_host.window, fullscreen);
        g_host.fullscreen_applied = fullscreen;
    }
    int scale = c.get_int("video", "window_scale", 1, 16);
    bool resize = g_host.wide != was_wide || (!g_host.wide && !g_host.env_scale && scale != g_host.window_scale_applied);
    if (resize && !(SDL_GetWindowFlags(g_host.window) & SDL_WINDOW_FULLSCREEN)) {
        int w, h;
        default_window_size(g_host.env_scale ? int(env_u64("NTR_SCALE")) : scale, w, h);
        SDL_SetWindowSize(g_host.window, w, h);
        SDL_SetWindowPosition(g_host.window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    }
    if (!g_host.env_scale) g_host.window_scale_applied = scale;
    if (!g_host.env_scale_3d) gpu3d_set_scale(c.get_int("video", "resolution_3d", 1, kMax3dScale));
    load_bindings();
}

// --- pause menu -------------------------------------------------------------------------

// "Reiniciar o jogo": the same command line in a new process, then this one exits. The
// log and the session recording are closed first so the new game can keep them as
// boej.old.log / boej-replay.old.*.
void restart() {
    std::vector<std::string> args = g_host.args;
    if (args.empty()) {
#ifdef _WIN32
        args.push_back(host_base_path() + "boej.exe");
#else
        args.push_back(host_base_path() + "boej");
#endif
    }
    std::vector<const char*> argv;
    for (const std::string& a : args) argv.push_back(a.c_str());
    argv.push_back(nullptr);
    NTR_LOG("[host] restarting the game\n");
    replay_finish(g_host.frame);
    log_close();
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, argv.data());
    SDL_SetBooleanProperty(props, SDL_PROP_PROCESS_CREATE_BACKGROUND_BOOLEAN, true);
    SDL_Process* process = SDL_CreateProcessWithProperties(props);
    SDL_DestroyProperties(props);
    if (!process) {
        NTR_LOG("[host] cannot restart: %s\n", SDL_GetError());
        return;
    }
    SDL_Quit();
    std::exit(0);
}

// The menu key or gamepad button, pressed again inside the menu.
bool is_menu_input(const SDL_Event& ev) {
    if (ev.type == SDL_EVENT_KEY_DOWN && !ev.key.repeat) {
        for (const KeyBinding& b : g_host.key_bindings) {
            if (b.action == kMenu && b.key == ev.key.key) return true;
        }
    }
    if (ev.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
        for (const PadBinding& b : g_host.pad_bindings) {
            if (b.action == kMenu && b.button == ev.gbutton.button) return true;
        }
    }
    return false;
}

// The last frame, dimmed, behind the menu.
void draw_paused_frame() {
    SDL_Renderer* r = g_host.renderer;
    SDL_SetRenderLogicalPresentation(r, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
    SDL_RenderClear(r);
    int w = 0, h = 0;
    SDL_GetRenderOutputSize(r, &w, &h);
    if (g_host.textures[0] && w > 0 && h > 0) {
        int lw, lh;
        layout_size(lw, lh);
        float fit = std::min(float(w) / lw, float(h) / lh);
        draw_screens((w - lw * fit) / 2, (h - lh * fit) / 2, fit);
    }
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 170);
    SDL_RenderFillRect(r, nullptr);
}

// Runs until the player resumes: the game is stopped inside host_present, so nothing
// advances (the clock included) and the sound is paused.
void run_menu() {
    g_host.menu_requested = false;
    if (!g_host.ui_ready) {
        float scale = SDL_GetWindowDisplayScale(g_host.window);
        ui::init(g_host.window, g_host.renderer, scale > 0 ? scale : 1);
        g_host.ui_ready = true;
    }
    if (g_host.audio) SDL_PauseAudioStreamDevice(g_host.audio);
    NTR_LOG("[host] paused at frame %llu\n", (unsigned long long)g_host.frame);
    ui::SettingsOptions options;
    options.in_game = true;
    options.on_change = apply_settings;
    // Saving or loading closes the menu, so the slots are read once.
    std::vector<std::string> states;
    std::vector<bool> loadable;
    for (int slot = 1; slot <= kStateSlots; slot++) {
        states.push_back(savestate_describe(slot));
        loadable.push_back(!states.back().empty() && std::isdigit(uint8_t(states.back()[0])));
    }
    for (bool open = true; open;) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_EVENT_QUIT) quit();
            if (ev.type == SDL_EVENT_GAMEPAD_ADDED || ev.type == SDL_EVENT_GAMEPAD_REMOVED) pad_connection(ev);
            bool capture_input = ev.type == SDL_EVENT_KEY_DOWN || ev.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN ||
                                 ev.type == SDL_EVENT_GAMEPAD_AXIS_MOTION;
            if (ui::capturing() && capture_input) {
                ui::capture_event(ev);  // the input is the new binding
                continue;
            }
            if (is_menu_input(ev)) {
                open = false;
                continue;
            }
            ui::process_event(ev);
        }
        diag_frame(g_host.frame);  // paused, not stuck: keeps the watchdog quiet
        draw_paused_frame();
        ui::begin_frame();
        int slot = 0;
        ui::PauseAction action = ui::pause_menu(g_host.config, g_host.config_path, options, states, loadable, slot);
        ui::end_frame(g_host.renderer);
        SDL_RenderPresent(g_host.renderer);
        if (action == ui::PauseAction::Resume) open = false;
        if (action == ui::PauseAction::SaveState || action == ui::PauseAction::LoadState) {
            g_host.state_slot = slot;  // the shortcuts go on with this slot
            if (action == ui::PauseAction::SaveState) savestate_request_save(slot);
            else savestate_request_load(slot);
            open = false;
        }
        if (action == ui::PauseAction::Restart) restart();
        if (action == ui::PauseAction::Quit) quit();
        SDL_Delay(16);
    }
    apply_presentation();
    if (g_host.audio) SDL_ResumeAudioStreamDevice(g_host.audio);
    // Keys pressed or released while paused went to the menu: start from what is held now.
    g_host.keys_down.clear();
    update_actions();
    g_host.deadline = g_host.work_start = SDL_GetTicksNS();
    NTR_LOG("[host] resumed\n");
}

}  // namespace

bool host_fast_forward() { return fast_forward(); }

void host_set_command_line(const std::vector<std::string>& args) { g_host.args = args; }

std::string host_base_path() {
    const char* p = SDL_GetBasePath();
    return p ? p : "";
}

bool host_init() {
    for (const char* name : {"NTR_SCREENSHOT_FRAME", "NTR_SCREENSHOT_FRAMES"}) {
        const char* list = std::getenv(name);
        for (const char* p = list; p && *p;) {
            char* end;
            uint64_t f = std::strtoull(p, &end, 10);
            if (end == p) break;
            g_host.screenshot_frames.insert(f);
            p = *end == ',' ? end + 1 : end;
        }
    }
    if (const char* p = std::getenv("NTR_SCREENSHOT")) g_host.screenshot_path = p;
    g_host.exit_frame = env_u64("NTR_EXIT_FRAME");
    g_host.state_save_frame = env_u64("NTR_STATE_SAVE");
    g_host.state_load_frame = env_u64("NTR_STATE_LOAD");
    g_host.throttle = !std::getenv("NTR_FAST");
    if (const char* s = std::getenv("NTR_INPUT")) parse_script(s);
    if (const char* path = std::getenv("NTR_REPLAY")) load_replay(path);
    if (const char* s = std::getenv("NTR_PEEK")) std::sscanf(s, "%x:%u", &g_host.peek_addr, &g_host.peek_size);
    if (const char* p = std::getenv("NTR_WAV")) {
        g_host.wav = std::fopen(p, "wb");
        if (g_host.wav) write_wav_header(g_host.wav, 0);
    }
    bool headless = std::getenv("NTR_HEADLESS") != nullptr;
    bool scripted = std::getenv("NTR_INPUT") || std::getenv("NTR_REPLAY");
    if ((!headless && !scripted) || std::getenv("NTR_RECORD")) replay_enable();
    diag_init(!headless);
    const char* config_env = std::getenv("NTR_CONFIG");
    std::string config_path = config_env ? config_env : host_base_path() + "boej.ini";
    g_host.config_path = config_path;
    g_host.env_scale = env_u64("NTR_SCALE") > 0;
    g_host.env_scale_3d = env_u64("NTR_3D_SCALE") > 0;
    if (config_env || !headless) {
        if (!config_load(config_path, g_host.config) && !headless) config_write_default(config_path);
    } else {
        config_load("", g_host.config);  // defaults only
    }
    const Config& config = g_host.config;
    int scale_3d = int(env_u64("NTR_3D_SCALE"));
    if (scale_3d <= 0) scale_3d = headless && !config_env ? 1 : config.get_int("video", "resolution_3d", 1, kMax3dScale);
    gpu3d_set_scale(scale_3d);
    g_host.ff_speed = config.get_int("game", "fast_forward_speed", 2, 10);
    if (headless) return true;

    SDL_SetMainReady();
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        NTR_LOG("[host] SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }
    int scale = int(env_u64("NTR_SCALE"));
    if (scale <= 0) scale = config.get_int("video", "window_scale", 1, 16);
    SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE;
    if (config.get_bool("video", "fullscreen")) flags |= SDL_WINDOW_FULLSCREEN;
    g_host.wide = config.get_bool("video", "wide_layout");
    int win_w, win_h;
    default_window_size(scale, win_w, win_h);
    if (!SDL_CreateWindowAndRenderer("Inazuma Eleven 3 - The Ogre (recomp)", win_w, win_h, flags, &g_host.window,
                                     &g_host.renderer)) {
        NTR_LOG("[host] cannot create the window: %s\n", SDL_GetError());
        return false;
    }
    std::string filter = config.get("video", "filter");
    if (SDL_strcasecmp(filter.c_str(), "linear") != 0 && SDL_strcasecmp(filter.c_str(), "nearest") != 0) {
        config.warn(*config.find("video", "filter"), "\"" + filter + "\" is not nearest or linear");
    }
    apply_presentation();
    g_host.fullscreen_applied = config.get_bool("video", "fullscreen");
    g_host.window_scale_applied = scale;
    load_bindings();
    g_host.headless = false;
    open_audio();
    g_host.deadline = g_host.fps_start = SDL_GetTicksNS();
    return true;
}

namespace {

// --- first run: preparing boej.pak ------------------------------------------------------

[[noreturn]] void quit_setup() {
    NTR_LOG("[setup] closed by the player\n");
    SDL_Quit();
    std::exit(0);
}

// The setup screen; SDL's debug font has ASCII only. progress < 0 = no bar.
void draw_setup(const std::vector<const char*>& lines, double progress) {
    SDL_Renderer* r = g_host.renderer;
    SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
    SDL_RenderClear(r);
    SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
    float y = 150;
    for (const char* line : lines) {
        SDL_RenderDebugText(r, (kScreenWidth - 8.0f * float(std::strlen(line))) / 2, y, line);
        y += 14;
    }
    if (progress >= 0) {
        SDL_FRect frame = {28, y + 10, 200, 12};
        SDL_RenderRect(r, &frame);
        SDL_FRect bar = {30, y + 12, float(196 * std::min(progress, 1.0)), 8};
        SDL_SetRenderDrawColor(r, 80, 160, 255, 255);
        SDL_RenderFillRect(r, &bar);
    }
    SDL_RenderPresent(r);
}

// Keeps the window responsive until `done`.
void wait_setup(const std::atomic<bool>& done, const std::vector<const char*>& lines,
                const std::atomic<double>* progress) {
    while (!done) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_EVENT_QUIT) quit_setup();
        }
        draw_setup(lines, progress ? progress->load() : -1);
        SDL_Delay(16);
    }
}

void setup_message(SDL_MessageBoxFlags flags, const std::string& text) {
    SDL_ShowSimpleMessageBox(flags, "Inazuma Eleven 3 - The Ogre (recomp)", text.c_str(), g_host.window);
}

// Asks for the dump: a message, then the file dialog. False if the player gives up.
bool choose_dump(std::string& path) {
    const SDL_MessageBoxButtonData buttons[] = {
        {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Escolher o dump..."},
        {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Sair"},
    };
    const SDL_MessageBoxData box = {
        SDL_MESSAGEBOX_INFORMATION, g_host.window, "Inazuma Eleven 3 - The Ogre (recomp)",
        "Para jogar, escolha o dump do seu cartucho de Inazuma Eleven 3 - Sekai heno Chousen!! "
        "The Ogre (arquivo .nds, código BOEJ).\n\n"
        "Ele é usado só agora, para criar o boej.pak (cerca de 450 MB) na pasta do jogo. "
        "Depois disso, o jogo não precisa mais do dump.",
        2, buttons, nullptr};
    int choice = 0;
    if (!SDL_ShowMessageBox(&box, &choice) || choice != 1) return false;

    struct Dialog {
        std::atomic<bool> done{false};
        std::string path, error;
    } dialog;
    static const SDL_DialogFileFilter kFilters[] = {{"Dump de Nintendo DS (*.nds)", "nds"}, {"Todos os arquivos", "*"}};
    SDL_ShowOpenFileDialog(
        [](void* user, const char* const* files, int) {
            auto* d = static_cast<Dialog*>(user);
            if (!files) d->error = SDL_GetError();
            else if (files[0]) d->path = files[0];
            d->done = true;
        },
        &dialog, g_host.window, kFilters, 2, nullptr, false);
    wait_setup(dialog.done, {"Primeira execucao", "", "Escolha o dump do jogo (.nds)", "na janela que abriu."}, nullptr);
    if (!dialog.error.empty()) {
        NTR_LOG("[setup] no file dialog: %s\n", dialog.error.c_str());
        setup_message(SDL_MESSAGEBOX_ERROR,
                      "Não foi possível abrir a janela de escolha de arquivo (" + dialog.error + ").\n\n"
                      "Arraste o arquivo .nds para cima do executável do jogo.");
        return false;
    }
    path = dialog.path;
    return !path.empty();
}

}  // namespace

bool host_first_run(const std::string& nds_path, const std::string& pak_path) {
    if (g_host.headless) return false;
    for (;;) {
        std::string dump = nds_path;
        if (dump.empty() && !choose_dump(dump)) return false;
        std::atomic<bool> done{false};
        std::atomic<double> progress{0};
        std::string error;
        bool ok = false;
        std::thread worker([&] {
            ok = pak_create(dump, pak_path, error, [&](double p) { progress = p; });
            done = true;
        });
        wait_setup(done, {"Preparando os dados do jogo", "(so na primeira vez)"}, &progress);
        worker.join();
        if (ok) return true;
        setup_message(SDL_MESSAGEBOX_ERROR, error);
        if (!nds_path.empty()) return false;
    }
}

// What the game sees until the next frame, for the session recording.
static void record_input() {
    replay_record(g_host.frame, pressed_keys(), g_host.touching || g_host.script_touch, g_host.touch_x,
                  g_host.touch_y);
}

// After a save or load (savestate.cpp), before the frame is shown.
void savestate_finished() {
    if (g_savestate_loaded) {
        g_savestate_loaded = false;
        // The game is somewhere else: what is queued would play over it, and the recording
        // can no longer be replayed from boot.
        if (g_host.audio) SDL_ClearAudioStream(g_host.audio);
        g_host.deadline = g_host.work_start = SDL_GetTicksNS();
        if (!g_host.headless) NTR_LOG("[host] savestate loaded at frame %llu\n", (unsigned long long)g_host.frame);
        replay_finish(g_host.frame);
    }
    if (!g_savestate_message.empty()) {
        show_toast(g_savestate_message);
        g_savestate_message.clear();
    }
}

void host_present(const uint32_t* top, int top_scale, const uint32_t* bottom, int bottom_scale) {
    g_host.frame++;
    savestate_finished();
    if (g_host.frame == g_host.state_save_frame) savestate_request_save(1);
    if (g_host.frame == g_host.state_load_frame) savestate_request_load(1);
    diag_frame(g_host.frame);
    trace_frame(g_host.frame);
    apply_script();
    if (g_host.peek_size) {
        NTR_LOG("[peek] frame %llu %08X:", (unsigned long long)g_host.frame, g_host.peek_addr);
        for (uint32_t i = 0; i < g_host.peek_size; i++) {
            // MMIO comes from the register file: reads with side effects would advance time.
            uint32_t a = g_host.peek_addr + i;
            uint8_t v = (a >> 24) == 0x04 && a - 0x04000000 < kIoSize ? memory().io[a - 0x04000000] : rd8(a);
            NTR_LOG(" %02X", v);
        }
        NTR_LOG("\n");
    }
    // Captures are as large as the 3D resolution, whatever each screen shows.
    int bmp_scale = std::max({top_scale, bottom_scale, gpu3d_scale()});
    if (g_host.screenshot_frames.count(g_host.frame)) {  // like F8: picture plus state dump
        write_bmp(frame_path(g_host.screenshot_path), top, top_scale, bottom, bottom_scale, bmp_scale);
        debug_dump("screenshot");
    }
    if (g_host.frame == g_host.f12_frame) {
        write_bmp(frame_path("boej-%d.bmp"), top, top_scale, bottom, bottom_scale, bmp_scale);
    }
    if (g_host.exit_frame && g_host.frame >= g_host.exit_frame) {
        NTR_LOG("[host] exiting after %llu frames\n", (unsigned long long)g_host.frame);
        quit();
    }
    if (g_host.headless) {
        uint64_t now = SDL_GetTicksNS();
        if (g_host.run_last) {
            uint64_t ns = now - g_host.run_last;
            g_host.run_ns += ns;
            g_host.run_frames++;
            if (ns > kFrameNs) g_host.run_slow++;
            if (ns > g_host.run_worst_ns) {
                g_host.run_worst_ns = ns;
                g_host.run_worst_frame = g_host.frame;
            }
        }
        g_host.run_last = now;
        record_input();
        return;
    }
    if (fast_forward() && g_host.frame % uint64_t(g_host.ff_speed) != 0) {  // about 60 pictures a second
        pump_events();
        record_input();
        if (g_host.menu_requested) run_menu();
        pace();
        return;
    }

    const uint32_t* pictures[2] = {top, bottom};
    int scales[2] = {top_scale, bottom_scale};
    for (int i = 0; i < 2; i++) {
        SDL_Texture*& t = g_host.textures[i];
        if (g_host.texture_scales[i] != scales[i]) {
            if (t) SDL_DestroyTexture(t);
            t = SDL_CreateTexture(g_host.renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING,
                                  kScreenWidth * scales[i], kScreenHeight * scales[i]);
            SDL_SetTextureScaleMode(t, g_host.linear_filter ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);
            g_host.texture_scales[i] = scales[i];
        }
        SDL_UpdateTexture(t, nullptr, pictures[i], kScreenWidth * scales[i] * 4);
    }
    SDL_SetRenderDrawColor(g_host.renderer, 0, 0, 0, 255);
    SDL_RenderClear(g_host.renderer);
    draw_screens(0, 0, 1);
    draw_toast();
    SDL_RenderPresent(g_host.renderer);
    pump_events();
    record_input();
    if (g_host.menu_requested) run_menu();
    pace();
}

void host_audio_push(const int16_t* frames, size_t count) {
    if (g_host.wav) {
        std::fwrite(frames, 4, count, g_host.wav);
        g_host.wav_frames += uint32_t(count);
    }
    if (!g_host.audio) return;
    // Running ahead of real time (fast forward, NTR_FAST) would only grow the latency.
    int queued = SDL_GetAudioStreamQueued(g_host.audio);
    if (queued > kSoundRate * 4 / 6) return;
    // Pushed once per frame: a late frame lets the queue run dry, and the sound would keep
    // stopping at every small delay after that. Two frames of silence ahead of the next
    // push give it room again (about 33 ms more latency).
    if (queued < int(count) * 4 && !fast_forward()) {
        static const std::vector<int16_t> silence(kSoundRate / 30 * 2);
        SDL_PutAudioStreamData(g_host.audio, silence.data(), int(silence.size() * 2));
    }
    SDL_PutAudioStreamData(g_host.audio, frames, int(count * 4));
}

uint16_t host_keyinput() { return uint16_t(~pressed_keys() & 0x3FF); }

uint16_t host_extkeys() {
    uint16_t pressed = uint16_t(pressed_keys() >> 10) & 3;
    uint16_t v = 0x007F & ~pressed;          // X, Y released; hinge open (bit 7 = 0)
    if (g_host.touching || g_host.script_touch) v &= ~(1u << 6);  // pen down
    return v;
}

bool host_touch(int& x, int& y) {
    x = g_host.touch_x;
    y = g_host.touch_y;
    return g_host.touching || g_host.script_touch;
}

}  // namespace ntr
