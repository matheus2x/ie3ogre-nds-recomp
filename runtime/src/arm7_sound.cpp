// ARM7 sound driver, high level (NitroSDK SND commands over PXI tag 7) and the 16 sound
// channels it drives.
//
// SND_FlushCommand sends the address of a linked list of SNDCommand:
//   +0 next, +4 id, +8 arg[4]
// The ARM9 waits until the finished-command tag in the shared work area advances.
//
// This game does not use the SDK sequencer: it locks every channel and runs its own
// mixer on the ARM9, paced by an SND alarm (a periodic PXI message "alarm | id << 8").
// So the driver here implements the channel, capture, timer and alarm commands; the
// sequencer commands (0-11) are acknowledged and logged once.
//
// Channels are mixed from guest memory as emulated time passes (32768 Hz, stereo) and
// the samples go to the host audio device once per frame.
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>
#include <vector>

#include "pxi.h"
#include "runtime.h"

namespace ntr {

namespace {

constexpr double kArm7Clock = 33513982.0;   // sound timers count at half of it
constexpr double kCyclesPerSample = kArm7Clock / kSoundRate;  // emulated bus cycles per output sample
constexpr uint32_t kOsTickCycles = 64;      // OS ticks (alarm units) are the bus clock / 64

enum Command : uint32_t {
    START_TIMER = 12, STOP_TIMER = 13, SETUP_CHANNEL_PCM = 14, SETUP_CHANNEL_PSG = 15,
    SETUP_CHANNEL_NOISE = 16, SETUP_CAPTURE = 17, SETUP_ALARM = 18, CHANNEL_TIMER = 19,
    CHANNEL_VOLUME = 20, CHANNEL_PAN = 21, SURROUND_DECAY = 22, MASTER_VOLUME = 23, MASTER_PAN = 24,
    OUTPUT_SELECTOR = 25, LOCK_CHANNEL = 26, UNLOCK_CHANNEL = 27, STOP_UNLOCKED_CHANNEL = 28,
    SHARED_WORK = 29, INVALIDATE_SEQ = 30, INVALIDATE_BANK = 31, INVALIDATE_WAVE = 32, READ_DRIVER_INFO = 33,
};

enum Format { PCM8 = 0, PCM16 = 1, ADPCM = 2, PSG = 3 };

struct Channel {
    bool active = false;
    uint32_t format = PCM8;
    uint32_t loop = 0;          // 0 manual, 1 repeat, 2 one-shot
    uint32_t addr = 0;
    uint32_t loop_start = 0;    // bytes
    uint32_t length = 0;        // bytes after loop_start
    uint32_t volume = 127, shift = 0, pan = 64;
    uint32_t timer = 0;         // period in 16.76 MHz ticks (the SDK passes it; the register gets 0x10000 - it)
    uint32_t duty = 0;          // PSG
    bool noise = false;

    double pos = 0;             // samples since start
    // IMA-ADPCM decoder state, and the state saved at the loop start.
    int32_t adpcm_sample = 0, adpcm_index = 0, adpcm_decoded = -1;
    int32_t loop_sample = 0, loop_index = 0;
    bool loop_saved = false;
    uint32_t lfsr = 0x7FFF;
    uint32_t noise_steps = 0;
    int16_t noise_out = 0;
};

struct Capture {
    bool active = false;
    uint32_t addr = 0, length = 0;  // bytes
    bool pcm8 = false, loop = false, from_channel = false, add = false;
    uint32_t pos = 0;
};

struct Alarm {
    bool armed = false, running = false;
    uint64_t tick = 0, period = 0;
    uint32_t id = 0;
    uint64_t next = 0;  // emulated cycle of the next message
};

struct Driver {
    Channel ch[16];
    Capture cap[2];
    Alarm alarms[8];
    uint32_t shared_work = 0;
    uint32_t master_volume = 127;
    double sample_clock = 0;     // emulated cycles not yet turned into samples
    uint64_t last_cycles = 0;
    std::vector<int16_t> out;    // interleaved stereo, flushed each frame
    std::map<uint32_t, bool> warned;
};
Driver g_snd;

uint16_t channel_status() {
    uint16_t s = 0;
    for (int i = 0; i < 16; i++) s |= g_snd.ch[i].active ? 1u << i : 0;
    return s;
}

void update_shared_work() {
    if (!g_snd.shared_work) return;
    wr16(g_snd.shared_work + 8, channel_status());
    wr16(g_snd.shared_work + 10, uint16_t((g_snd.cap[0].active ? 1 : 0) | (g_snd.cap[1].active ? 2 : 0)));
}

// --- sample generation ------------------------------------------------------------------

double channel_rate(const Channel& c) {
    return kArm7Clock / 2 / c.timer;  // samples (or PSG steps) per second
}

const int kAdpcmIndex[8] = {-1, -1, -1, -1, 2, 4, 6, 8};
const int16_t kAdpcmStep[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88,
    97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658,
    724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660,
    4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818,
    18500, 20350, 22385, 24623, 27086, 29794, 32767};

// Decodes ADPCM nibbles up to sample index n (0-based, after the 4-byte header).
int16_t adpcm_sample_at(Channel& c, int32_t n) {
    if (n < c.adpcm_decoded) {  // looped back: restore the state saved at the loop start
        int32_t loop_n = int32_t(c.loop_start) * 2 - 8;
        c.adpcm_sample = c.loop_sample;
        c.adpcm_index = c.loop_index;
        c.adpcm_decoded = loop_n - 1;
    }
    while (c.adpcm_decoded < n) {
        int32_t i = c.adpcm_decoded + 1;
        uint8_t byte = rd8(c.addr + 4 + uint32_t(i) / 2);
        uint32_t nib = (i & 1) ? byte >> 4 : byte & 15;
        int32_t step = kAdpcmStep[c.adpcm_index];
        int32_t diff = step / 8;
        if (nib & 1) diff += step / 4;
        if (nib & 2) diff += step / 2;
        if (nib & 4) diff += step;
        c.adpcm_sample = std::clamp(c.adpcm_sample + ((nib & 8) ? -diff : diff), -0x7FFF, 0x7FFF);
        c.adpcm_index = std::clamp(c.adpcm_index + kAdpcmIndex[nib & 7], 0, 88);
        c.adpcm_decoded = i;
        if (!c.loop_saved && uint32_t(i + 1) == c.loop_start * 2 - 8) {
            c.loop_sample = c.adpcm_sample;
            c.loop_index = c.adpcm_index;
            c.loop_saved = true;
        }
    }
    return int16_t(c.adpcm_sample);
}

void channel_start(Channel& c) {
    c.active = true;
    c.pos = 0;
    c.lfsr = 0x7FFF;
    c.noise_steps = 0;
    if (c.format == ADPCM) {
        uint32_t header = rd32(c.addr);
        c.adpcm_sample = int16_t(header);
        c.adpcm_index = std::clamp(int32_t((header >> 16) & 0x7F), 0, 88);
        c.adpcm_decoded = -1;
        c.loop_saved = c.loop_start <= 4;
        c.loop_sample = c.adpcm_sample;
        c.loop_index = c.adpcm_index;
    }
}

// One output sample of a channel (-32768..32767), advancing it by `step` source samples.
int32_t channel_sample(Channel& c, double step) {
    if (c.format == PSG) {
        double steps = c.pos;
        c.pos += step;
        if (c.noise) {
            uint32_t target = uint32_t(c.pos);
            while (c.noise_steps < target) {
                c.noise_steps++;
                bool carry = c.lfsr & 1;
                c.lfsr >>= 1;
                if (carry) c.lfsr ^= 0x6000;
                c.noise_out = carry ? -0x7FFF : 0x7FFF;
            }
            return c.noise_out;
        }
        uint32_t phase = uint32_t(steps) & 7;
        return phase <= c.duty ? 0x7FFF : -0x7FFF;  // duty (n+1)/8 high
    }
    uint32_t bytes_per_sample = c.format == PCM16 ? 2 : 1;
    uint32_t loop_samples, total_samples;
    if (c.format == ADPCM) {
        loop_samples = c.loop_start > 4 ? (c.loop_start - 4) * 2 : 0;
        total_samples = loop_samples + c.length * 2;
    } else {
        loop_samples = c.loop_start / bytes_per_sample;
        total_samples = loop_samples + c.length / bytes_per_sample;
    }
    if (total_samples == 0) {
        c.active = false;
        return 0;
    }
    uint32_t n = uint32_t(c.pos);
    if (n >= total_samples) {
        if (c.loop == 1 && total_samples > loop_samples) {
            uint32_t span = total_samples - loop_samples;
            c.pos = loop_samples + std::fmod(c.pos - loop_samples, double(span));
            n = uint32_t(c.pos);
        } else {
            c.active = false;
            return 0;
        }
    }
    int32_t s;
    switch (c.format) {
        case PCM8: s = int8_t(rd8(c.addr + n)) << 8; break;
        case PCM16: s = int16_t(rd16(c.addr + n * 2)); break;
        default: s = adpcm_sample_at(c, int32_t(n)); break;
    }
    c.pos += step;
    return s;
}

void mix_one(int16_t* frame) {
    double left = 0, right = 0;
    for (Channel& c : g_snd.ch) {
        if (!c.active || c.timer == 0) continue;
        double step = channel_rate(c) / kSoundRate;
        int32_t s = channel_sample(c, step);
        static const double kShift[4] = {1.0, 0.5, 0.25, 1.0 / 16};
        double v = s / 32768.0 * (c.volume / 127.0) * kShift[c.shift & 3];
        double pan = (c.pan & 127) / 127.0;
        left += v * (1.0 - pan);
        right += v * pan;
    }
    double master = g_snd.master_volume / 127.0 * 0.5;  // headroom for 16 channels
    frame[0] = int16_t(std::clamp(left * master, -1.0, 1.0) * 32767);
    frame[1] = int16_t(std::clamp(right * master, -1.0, 1.0) * 32767);
    // Capture 0/1 records the left/right mix (the "mixer output" source).
    for (int i = 0; i < 2; i++) {
        Capture& cap = g_snd.cap[i];
        if (!cap.active || cap.length == 0) continue;
        int16_t v = frame[i];
        if (cap.pcm8) {
            wr8(cap.addr + cap.pos, uint8_t(v >> 8));
            cap.pos += 1;
        } else {
            wr16(cap.addr + cap.pos, uint16_t(v));
            cap.pos += 2;
        }
        if (cap.pos >= cap.length) {
            cap.pos = 0;
            if (!cap.loop) cap.active = false;
        }
    }
}

// --- commands -----------------------------------------------------------------------------

void warn_once(uint32_t id) {
    if (g_snd.warned[id]) return;
    g_snd.warned[id] = true;
    NTR_LOG("[snd] command %u is not implemented\n", id);
}

void start_or_stop(uint32_t ch_mask, uint32_t cap_mask, uint32_t alarm_mask, bool start) {
    for (int i = 0; i < 16; i++) {
        if (!(ch_mask & (1u << i))) continue;
        if (start) channel_start(g_snd.ch[i]);
        else g_snd.ch[i].active = false;
    }
    for (int i = 0; i < 2; i++) {
        if (!(cap_mask & (1u << i))) continue;
        g_snd.cap[i].active = start;
        g_snd.cap[i].pos = 0;
    }
    for (int i = 0; i < 8; i++) {
        if (!(alarm_mask & (1u << i))) continue;
        Alarm& a = g_snd.alarms[i];
        if (start && a.armed) {
            a.running = true;
            a.next = now_cycles() + a.tick * kOsTickCycles;
        } else {
            a.running = false;
        }
    }
    update_shared_work();
}

void execute(uint32_t id, const uint32_t* a) {
    switch (id) {
        case START_TIMER: start_or_stop(a[0], a[1], a[2], true); break;
        case STOP_TIMER: start_or_stop(a[0], a[1], a[2], false); break;
        case SETUP_CHANNEL_PCM: {
            Channel& c = g_snd.ch[a[0] & 15];
            c.timer = a[0] >> 16;
            c.addr = a[1];
            c.length = (a[2] & 0x3FFFFF) * 4;
            c.shift = (a[2] >> 22) & 3;
            c.volume = (a[2] >> 24) & 127;
            c.loop_start = (a[3] & 0xFFFF) * 4;
            c.pan = (a[3] >> 16) & 127;
            c.format = (a[3] >> 24) & 3;
            c.loop = (a[3] >> 26) & 3;
            c.noise = false;
            break;
        }
        case SETUP_CHANNEL_PSG:
        case SETUP_CHANNEL_NOISE: {
            Channel& c = g_snd.ch[a[0] & 15];
            c.format = PSG;
            c.noise = id == SETUP_CHANNEL_NOISE;
            c.volume = a[1] & 127;
            c.shift = (a[1] >> 8) & 3;
            c.pan = a[2] & 127;
            c.timer = (a[2] >> 8) & 0xFFFF;
            c.duty = id == SETUP_CHANNEL_PSG ? a[3] & 7 : 0;
            break;
        }
        case SETUP_CAPTURE: {
            Capture& cap = g_snd.cap[(a[2] >> 31) & 1];
            cap.addr = a[0];
            cap.length = (a[1] & 0xFFFF) * 4;
            cap.pcm8 = (a[2] >> 30) & 1;
            cap.loop = !((a[2] >> 29) & 1);  // SDK: loop flag set = one-shot
            cap.from_channel = (a[2] >> 28) & 1;
            cap.add = (a[2] >> 27) & 1;
            break;
        }
        case SETUP_ALARM: {
            Alarm& al = g_snd.alarms[a[0] & 7];
            al.armed = true;
            al.running = false;
            al.tick = a[1];
            al.period = a[2];
            al.id = a[3] & 0xFF;
            break;
        }
        case CHANNEL_TIMER:
            for (int i = 0; i < 16; i++) {
                if (a[0] & (1u << i)) g_snd.ch[i].timer = a[1] & 0xFFFF;
            }
            break;
        case CHANNEL_VOLUME:
            for (int i = 0; i < 16; i++) {
                if (!(a[0] & (1u << i))) continue;
                g_snd.ch[i].volume = a[1] & 127;
                g_snd.ch[i].shift = a[2] & 3;
            }
            break;
        case CHANNEL_PAN:
            for (int i = 0; i < 16; i++) {
                if (a[0] & (1u << i)) g_snd.ch[i].pan = a[1] & 127;
            }
            break;
        case MASTER_VOLUME: g_snd.master_volume = a[0] & 127; break;
        case SHARED_WORK: g_snd.shared_work = a[0]; break;
        case LOCK_CHANNEL: case UNLOCK_CHANNEL: case STOP_UNLOCKED_CHANNEL: case MASTER_PAN:
        case SURROUND_DECAY: case OUTPUT_SELECTOR: case INVALIDATE_SEQ: case INVALIDATE_BANK:
        case INVALIDATE_WAVE:
            break;  // only matter to the sequencer, which this game does not use
        default:
            warn_once(id);
            break;
    }
}

void on_command_list(uint32_t list, bool) {
    static const bool dump = std::getenv("NTR_DUMP_SND");
    for (uint32_t cmd = list, guard = 0; cmd && guard < 1024; cmd = rd32(cmd), guard++) {
        uint32_t id = rd32(cmd + 4);
        uint32_t args[4] = {rd32(cmd + 8), rd32(cmd + 12), rd32(cmd + 16), rd32(cmd + 20)};
        if (dump) NTR_LOG("[snd] cmd %2u %08X %08X %08X %08X\n", id, args[0], args[1], args[2], args[3]);
        sound_advance();  // channels set up now start from this point in time
        execute(id, args);
    }
    if (g_snd.shared_work) wr32(g_snd.shared_work, rd32(g_snd.shared_work) + 1);
    update_shared_work();
}

}  // namespace

void arm7_sound_init() { pxi_register_service(7, on_command_list); }

// Mixes up to the current emulated time and delivers due alarms.
void sound_advance() {
    uint64_t now = now_cycles();
    if (now <= g_snd.last_cycles) return;
    g_snd.sample_clock += double(now - g_snd.last_cycles);
    g_snd.last_cycles = now;
    while (g_snd.sample_clock >= kCyclesPerSample) {
        g_snd.sample_clock -= kCyclesPerSample;
        int16_t frame[2];
        mix_one(frame);
        g_snd.out.push_back(frame[0]);
        g_snd.out.push_back(frame[1]);
    }
    for (int i = 0; i < 8; i++) {
        Alarm& a = g_snd.alarms[i];
        while (a.running && now >= a.next) {
            pxi_reply(7, uint32_t(i) | a.id << 8);
            if (a.period == 0) {
                a.running = false;
                break;
            }
            a.next += a.period * kOsTickCycles;
        }
    }
}

void sound_vblank() {
    sound_advance();
    update_shared_work();
    if (!g_snd.out.empty()) host_audio_push(g_snd.out.data(), g_snd.out.size() / 2);
    g_snd.out.clear();
}

}  // namespace ntr
