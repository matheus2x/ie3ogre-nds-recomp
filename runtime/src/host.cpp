// Host frontend on SDL3: one window with both screens stacked, keyboard/gamepad/mouse
// input and frame pacing to the DS refresh rate (59.8261 Hz).
//
// Keys: arrows = D-pad, X = A, Z = B, S = X, A = Y, Q = L, W = R, Enter = Start,
// Right Shift/Backspace = Select, Tab (held) = fast forward, F12 = screenshot.
// The left mouse button touches the bottom screen.
//
// Environment: NTR_HEADLESS (no window, unthrottled), NTR_FAST (unthrottled), NTR_MUTE (no
// audio device), NTR_WAV (also write the sound output to that WAV file),
// NTR_SCALE (initial window scale, default 2), NTR_SCREENSHOT_FRAMES + NTR_SCREENSHOT
// (save those frames, a comma-separated list, as BMPs; %d in the path is the frame), NTR_EXIT_FRAME (exit after that many frames),
// NTR_INPUT (scripted input for tests: "first-last:KEY[+KEY...];..." with frame numbers
// and keys A B X Y L R START SELECT UP DOWN LEFT RIGHT or TOUCH@x,y), NTR_PEEK
// ("hexaddr:size", dumps guest memory every frame).
#define SDL_MAIN_HANDLED  // main() is ours; SDL_SetMainReady() is called instead
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <set>
#include <string>
#include <vector>

#include <ntr/runtime.h>

#include "runtime.h"

namespace ntr {

namespace {

constexpr uint64_t kFrameNs = 1000000000ull * 263 * 2130 / 33513982;  // 355x263 dots, 6 cycles each

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
    SDL_Texture* texture = nullptr;
    uint64_t frame = 0;
    uint64_t deadline = 0;
    uint64_t fps_start = 0, fps_frames = 0;
    std::set<uint64_t> screenshot_frames;
    std::string screenshot_path = "boej-%d.bmp";  // %d = frame number
    uint64_t f12_frame = 0, exit_frame = 0;

    uint16_t keys = 0;     // pressed DS buttons: KEYINPUT bits 0-9, X = bit 10, Y = bit 11
    uint16_t pad_keys = 0;
    bool touching = false;
    int touch_x = 0, touch_y = 0;
    bool fast_forward = false;
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

void write_bmp(const std::string& path, const uint32_t* top, const uint32_t* bottom) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) {
        NTR_LOG("[host] cannot write %s\n", path.c_str());
        return;
    }
    const uint32_t w = kScreenWidth, h = kScreenHeight * 2, row = w * 3, size = 54 + row * h;
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
        const uint32_t* src = y < kScreenHeight ? top + y * w : bottom + (y - kScreenHeight) * w;
        for (uint32_t x = 0; x < w; x++) {
            uint8_t bgr[3] = {uint8_t(src[x]), uint8_t(src[x] >> 8), uint8_t(src[x] >> 16)};
            std::fwrite(bgr, 1, 3, f);
        }
    }
    std::fclose(f);
    NTR_LOG("[host] frame %llu saved to %s\n", (unsigned long long)g_host.frame, path.c_str());
}

uint16_t key_by_name(const std::string& name) {
    static const char* const kNames[] = {"A", "B", "SELECT", "START", "RIGHT", "LEFT", "UP", "DOWN", "R", "L", "X", "Y"};
    for (int i = 0; i < 12; i++) {
        if (name == kNames[i]) return uint16_t(1u << i);
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

uint16_t pressed_keys() { return g_host.keys | g_host.pad_keys | g_host.script_keys; }

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
    if (g_host.wav) {
        write_wav_header(g_host.wav, g_host.wav_frames);
        std::fclose(g_host.wav);
    }
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
    SDL_ResumeAudioStreamDevice(g_host.audio);
}

uint16_t key_bit(SDL_Keycode key) {
    switch (key) {
        case SDLK_X: return 1u << 0;        // A
        case SDLK_Z: return 1u << 1;        // B
        case SDLK_RSHIFT:
        case SDLK_BACKSPACE: return 1u << 2;  // Select
        case SDLK_RETURN: return 1u << 3;   // Start
        case SDLK_RIGHT: return 1u << 4;
        case SDLK_LEFT: return 1u << 5;
        case SDLK_UP: return 1u << 6;
        case SDLK_DOWN: return 1u << 7;
        case SDLK_W: return 1u << 8;        // R
        case SDLK_Q: return 1u << 9;        // L
        case SDLK_S: return 1u << 10;       // X
        case SDLK_A: return 1u << 11;       // Y
        default: return 0;
    }
}

uint16_t gamepad_bit(int button) {
    switch (button) {
        case SDL_GAMEPAD_BUTTON_EAST: return 1u << 0;
        case SDL_GAMEPAD_BUTTON_SOUTH: return 1u << 1;
        case SDL_GAMEPAD_BUTTON_BACK: return 1u << 2;
        case SDL_GAMEPAD_BUTTON_START: return 1u << 3;
        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return 1u << 4;
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return 1u << 5;
        case SDL_GAMEPAD_BUTTON_DPAD_UP: return 1u << 6;
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return 1u << 7;
        case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return 1u << 8;
        case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return 1u << 9;
        case SDL_GAMEPAD_BUTTON_NORTH: return 1u << 10;
        case SDL_GAMEPAD_BUTTON_WEST: return 1u << 11;
        default: return 0;
    }
}

// Window position -> bottom screen pixel; false outside the bottom screen.
bool to_touch(float wx, float wy, int& x, int& y) {
    float lx, ly;
    if (!SDL_RenderCoordinatesFromWindow(g_host.renderer, wx, wy, &lx, &ly)) return false;
    if (lx < 0 || lx >= kScreenWidth || ly < kScreenHeight || ly >= 2 * kScreenHeight) return false;
    x = int(lx);
    y = int(ly) - kScreenHeight;
    return true;
}

void pump_events() {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
            case SDL_EVENT_QUIT:
                quit();
                break;
            case SDL_EVENT_KEY_DOWN:
            case SDL_EVENT_KEY_UP: {
                bool down = ev.type == SDL_EVENT_KEY_DOWN;
                if (ev.key.key == SDLK_TAB) g_host.fast_forward = down;
                if (ev.key.key == SDLK_F12 && down && !ev.key.repeat) {
                    g_host.f12_frame = g_host.frame + 1;
                }
                uint16_t bit = key_bit(ev.key.key);
                g_host.keys = down ? (g_host.keys | bit) : (g_host.keys & ~bit);
                break;
            }
            case SDL_EVENT_GAMEPAD_ADDED:
                SDL_OpenGamepad(ev.gdevice.which);
                break;
            case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
            case SDL_EVENT_GAMEPAD_BUTTON_UP: {
                uint16_t bit = gamepad_bit(ev.gbutton.button);
                g_host.pad_keys = ev.gbutton.down ? (g_host.pad_keys | bit) : (g_host.pad_keys & ~bit);
                break;
            }
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
    if (!g_host.throttle || g_host.fast_forward) {
        g_host.deadline = now;
    } else {
        g_host.deadline += kFrameNs;
        if (g_host.deadline > now) SDL_DelayPrecise(g_host.deadline - now);
        else if (now - g_host.deadline > 4 * kFrameNs) g_host.deadline = now;  // too far behind: resync
    }
    g_host.fps_frames++;
    if (now - g_host.fps_start >= 1000000000ull) {
        double fps = g_host.fps_frames * 1e9 / double(now - g_host.fps_start);
        char title[96];
        std::snprintf(title, sizeof title, "Inazuma Eleven 3 - The Ogre (recomp) - %.1f fps", fps);
        SDL_SetWindowTitle(g_host.window, title);
        g_host.fps_start = now;
        g_host.fps_frames = 0;
    }
}

}  // namespace

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
    g_host.throttle = !std::getenv("NTR_FAST");
    if (const char* s = std::getenv("NTR_INPUT")) parse_script(s);
    if (const char* s = std::getenv("NTR_PEEK")) std::sscanf(s, "%x:%u", &g_host.peek_addr, &g_host.peek_size);
    if (const char* p = std::getenv("NTR_WAV")) {
        g_host.wav = std::fopen(p, "wb");
        if (g_host.wav) write_wav_header(g_host.wav, 0);
    }
    if (std::getenv("NTR_HEADLESS")) return true;

    SDL_SetMainReady();
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        NTR_LOG("[host] SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }
    int scale = int(env_u64("NTR_SCALE"));
    if (scale <= 0) scale = 2;
    if (!SDL_CreateWindowAndRenderer("Inazuma Eleven 3 - The Ogre (recomp)", kScreenWidth * scale,
                                     kScreenHeight * 2 * scale, SDL_WINDOW_RESIZABLE, &g_host.window,
                                     &g_host.renderer)) {
        NTR_LOG("[host] cannot create the window: %s\n", SDL_GetError());
        return false;
    }
    SDL_SetRenderLogicalPresentation(g_host.renderer, kScreenWidth, kScreenHeight * 2,
                                     SDL_LOGICAL_PRESENTATION_LETTERBOX);
    g_host.texture = SDL_CreateTexture(g_host.renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING,
                                       kScreenWidth, kScreenHeight * 2);
    SDL_SetTextureScaleMode(g_host.texture, SDL_SCALEMODE_NEAREST);
    g_host.headless = false;
    open_audio();
    g_host.deadline = g_host.fps_start = SDL_GetTicksNS();
    return true;
}

void host_present(const uint32_t* top, const uint32_t* bottom) {
    g_host.frame++;
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
    if (g_host.screenshot_frames.count(g_host.frame)) write_bmp(frame_path(g_host.screenshot_path), top, bottom);
    if (g_host.frame == g_host.f12_frame) write_bmp(frame_path("boej-%d.bmp"), top, bottom);
    if (g_host.exit_frame && g_host.frame >= g_host.exit_frame) {
        NTR_LOG("[host] exiting after %llu frames\n", (unsigned long long)g_host.frame);
        quit();
    }
    if (g_host.headless) return;

    SDL_Rect upper = {0, 0, kScreenWidth, kScreenHeight};
    SDL_Rect lower = {0, kScreenHeight, kScreenWidth, kScreenHeight};
    SDL_UpdateTexture(g_host.texture, &upper, top, kScreenWidth * 4);
    SDL_UpdateTexture(g_host.texture, &lower, bottom, kScreenWidth * 4);
    SDL_SetRenderDrawColor(g_host.renderer, 0, 0, 0, 255);
    SDL_RenderClear(g_host.renderer);
    SDL_RenderTexture(g_host.renderer, g_host.texture, nullptr, nullptr);
    SDL_RenderPresent(g_host.renderer);
    pump_events();
    pace();
}

void host_audio_push(const int16_t* frames, size_t count) {
    if (g_host.wav) {
        std::fwrite(frames, 4, count, g_host.wav);
        g_host.wav_frames += uint32_t(count);
    }
    if (!g_host.audio) return;
    // Running ahead of real time (fast forward, NTR_FAST) would only grow the latency.
    if (SDL_GetAudioStreamQueued(g_host.audio) > kSoundRate * 4 / 6) return;
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
