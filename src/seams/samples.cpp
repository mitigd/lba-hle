// Digital sound seams: the game's sample API. The original reaches its wave driver through a
// table of 18 stubs (FUN_00028BD7 and up, each `mov eax,n; jmp [table+n*4]`) whose entries lived in
// the sound card's wave DLL: play a Creative VOC sample on a numbered channel, stop it, ask whether
// it still plays, and so on. The mixer behind them filled the card's DMA buffer. Native: each stub
// is a seam, and the mixer is a small software mixer feeding the host audio sink. The game's own
// logic - which sample, which volume and pan, when to stop - is untouched.
//
// Entry semantics were read from the original wave driver (w_sb16.dll), not guessed:
//   3  play(handle, pitch, loops, chain, volL, volR, voc)  -> id (handle | serial << 16)
//   4  set a value on a playing handle (stored, never read by the mixer)
//   5  stop all        6  stop(handle)        7  playing(handle)
//   9/10  mixer sync begin/end (double buffering)        16  block move that fixes sample pointers
//   0/1/2/14  init, shutdown, parameter list, buffer size
#include "seams/seams.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <vector>

namespace game::seams {
namespace {

using hlekit::Machine;
using hlekit::Reg;
using hlekit::Seam;

constexpr int kRate = 44100;
constexpr uint32_t kBufferFrames = 2048;
constexpr size_t kMaxChannels = 50;   // the driver's channel table

struct Channel {
    uint32_t id;
    std::vector<uint8_t> pcm;   // 8-bit unsigned, copied from the guest at play time
    double pos = 0;             // in source samples
    double step = 0;            // source samples per output frame
    uint32_t loops = 1;         // plays left; the original counts a 16-bit word down from 0 = 65536
    int volL = 0, volR = 0;
};

std::vector<Channel> g_channels;
uint16_t g_serial = 1;
bool g_sink = false;
std::chrono::steady_clock::time_point g_last;

// The first sound block of a VOC file, as the original driver reads it: block type 1, codec 0,
// frequency divisor at +4, length (including divisor and codec bytes) in 24 bits at +1.
bool ParseVoc(Machine& m, uint32_t at, std::vector<uint8_t>& pcm, double& rate) {
    const uint8_t* head = m.vm.Ptr(at, 0x20);
    if (!head) return false;
    const uint32_t block = at + head[0x14] + (head[0x15] << 8);
    const uint8_t* b = m.vm.Ptr(block, 6);
    if (!b || b[0] != 1 || b[5] != 0) return false;
    const uint32_t len = (b[1] | b[2] << 8 | b[3] << 16);
    if (len < 3) return false;
    const uint32_t n = len - 2;
    const uint8_t* data = m.vm.Ptr(block + 6, n);
    if (!data) return false;
    pcm.assign(data, data + n);
    rate = 1000000.0 / (256 - b[4]);
    return true;
}

uint32_t Play(Machine& m) {
    if (!m.vm.Rd32(D(m, kWaveOn))) return 0;
    const uint32_t handle = m.Arg(0) & 0xffff, pitch = m.Arg(1), loops = m.Arg(2);
    if (m.Arg(3)) m.Count("sample chain (unsupported)");
    Channel c;
    double rate = 0;
    if (!ParseVoc(m, m.Arg(6), c.pcm, rate)) return 0;
    if (g_channels.size() >= kMaxChannels) return 0;
    c.id = handle | ((uint32_t)g_serial++ << 16);
    c.step = rate * pitch / 4096.0 / kRate;
    c.loops = (loops & 0xffff) ? (loops & 0xffff) : 0x10000;
    c.volL = (int)(m.Arg(4) >> 2);
    c.volR = (int)(m.Arg(5) >> 2);
    g_channels.push_back(std::move(c));
    m.Count("sample play");
    return g_channels.back().id;
}

void Stop(uint32_t handle) {
    g_channels.erase(std::remove_if(g_channels.begin(), g_channels.end(),
                                    [&](const Channel& c) { return (c.id & 0xffff) == (handle & 0xffff); }),
                     g_channels.end());
}

bool Playing(uint32_t handle) {
    return std::any_of(g_channels.begin(), g_channels.end(),
                       [&](const Channel& c) { return (c.id & 0xffff) == (handle & 0xffff); });
}

// Stereo 16-bit frames: each channel contributes (sample - 128) * volume. Volume is the play
// argument >> 2 as the original stores it; x4 maps a full-volume full-scale sample to 16-bit full
// scale. Linear interpolation between source samples.
void Render(int16_t* out, uint32_t frames) {
    for (uint32_t f = 0; f < frames; ++f) {
        int l = 0, r = 0;
        for (Channel& c : g_channels) {
            if (c.loops == 0) continue;
            const size_t i = (size_t)c.pos;
            const double frac = c.pos - (double)i;
            const int s0 = (int)c.pcm[i] - 128;
            const int s1 = (int)c.pcm[i + 1 < c.pcm.size() ? i + 1 : i] - 128;
            const double s = s0 + (s1 - s0) * frac;
            l += (int)(s * c.volL * 4);
            r += (int)(s * c.volR * 4);
            c.pos += c.step;
            if (c.pos >= (double)c.pcm.size()) {
                if (--c.loops) c.pos = 0;
            }
        }
        out[f * 2] = (int16_t)std::clamp(l, -32768, 32767);
        out[f * 2 + 1] = (int16_t)std::clamp(r, -32768, 32767);
    }
    g_channels.erase(std::remove_if(g_channels.begin(), g_channels.end(), [](const Channel& c) { return c.loops == 0; }),
                     g_channels.end());
}

void FeedSink(Machine& m) {
    std::vector<int16_t> buf(kBufferFrames * 2);
    Render(buf.data(), kBufferFrames);
    const float g = m.gain;
    if (g < 0.999f)
        for (auto& v : buf) v = (int16_t)(v * g);
    if (m.audio) m.audio->Queue((const uint8_t*)buf.data(), buf.size() * sizeof(int16_t));
    g_last = std::chrono::steady_clock::now();
}

// Entries the game calls only when the device is on; the original stub returns 0 otherwise.
using Body = void (*)(Machine&);
Seam Entry(uint32_t addr, const char* name, const char* doc, Body body, bool gated = true) {
    return {addr, name, "sound", doc,
            [body, gated](Machine& m) {
                if (gated && !m.vm.Rd32(D(m, kWaveOn))) { m.vm.Set(Reg::Eax, 0); return; }
                body(m);
            },
            0, 0, Seam::Ret::Near32};
}

}  // namespace

void ServiceSamples(Machine& m) {
    // Channels must end on time even with no host sink (muted): advance them by the host clock.
    if (g_sink && m.audio) return;
    if (g_channels.empty()) { g_last = std::chrono::steady_clock::now(); return; }
    const auto now = std::chrono::steady_clock::now();
    uint32_t frames = (uint32_t)(std::chrono::duration<double>(now - g_last).count() * kRate);
    g_last = now;
    std::vector<int16_t> scratch;
    while (frames) {
        const uint32_t n = std::min<uint32_t>(frames, kBufferFrames);
        scratch.resize(n * 2);
        Render(scratch.data(), n);
        frames -= n;
    }
}

SeamList Samples() {
    return {
        {0x2f758, "wave_driver_load", "sound",
         "FUN_0002F758(file): loads the wave driver DLL named in LBA.CFG; the driver's table makes the "
         "stubs at 28BD7h live (42BD4h). Native: no DLL; the digital device is marked present.",
         [](Machine& m) { m.vm.Wr32(D(m, kWaveOn), 1); m.vm.Set(Reg::Eax, 1); }, 0, 0, Seam::Ret::Near32},
        {0x2f7e8, "wave_init", "sound",
         "FUN_0002F7E8: asks the driver (stub 14) for its buffer size, allocates it below 1 MB, and "
         "calls stub 0 to find the card and start DMA; the result is stored at 42BD4h. Native: open "
         "the host sink and start the software mixer.",
         [](Machine& m) {
             g_channels.clear();
             g_sink = m.StartSound(kRate, 2, 16, kBufferFrames * 4, [](Machine& mm) { FeedSink(mm); });
             g_last = std::chrono::steady_clock::now();
             m.vm.Wr32(D(m, kWaveOn), 1);
             m.vm.Set(Reg::Eax, 1);
         }, 0, 0, Seam::Ret::Near32},
        {0x28be5, "wave_params", "sound",
         "Stub 2 (FUN_00028BE5): the list of card settings (WaveBase, WaveIRQ, WaveDMA, WaveRate) the "
         "driver needs from LBA.CFG. Native: the list's empty terminating entry, none needed.",
         [](Machine& m) {
             m.vm.Wr32(m.Arg(0), D(m, kMidiParamNames) + 12);
             m.vm.Wr32(m.Arg(1), D(m, kMidiParamValues));
         }, 0, 0, Seam::Ret::Near32},
        Entry(0x28bde, "wave_shutdown", "Stub 1 (FUN_00028BDE): stop the card's DMA and release it. Native: silence the mixer.",
              [](Machine& m) { g_channels.clear(); m.StopSound(); }),
        Entry(0x28bec, "sample_play",
              "Stub 3 (FUN_00028BEC): play(handle, pitch, loops, chain, volL, volR, voc). Starts a new channel "
              "on the first sound block of the Creative VOC at `voc` and returns its id. Native: copy the "
              "samples and add a channel to the software mixer.",
              [](Machine& m) { m.vm.Set(Reg::Eax, Play(m)); }),
        Entry(0x28bf3, "sample_set", "Stub 4 (FUN_00028BF4): stores a value on a playing handle; the driver's mixer never reads it. Native: accepted.",
              [](Machine&) {}),
        Entry(0x28bfa, "sample_stop_all", "Stub 5 (FUN_00028BFA): clears every channel.",
              [](Machine&) { g_channels.clear(); }),
        Entry(0x28c01, "sample_stop", "Stub 6 (FUN_00028C01): stop(handle) - removes each channel whose handle matches.",
              [](Machine& m) { Stop(m.Arg(0)); }),
        Entry(0x28c08, "sample_playing", "Stub 7 (FUN_00028C08): 1 while a channel with that handle plays.",
              [](Machine& m) { m.vm.Set(Reg::Eax, Playing(m.Arg(0)) ? 1 : 0); }),
        Entry(0x28c16, "mixer_sync_begin",
              "Stub 9 (FUN_00028C16): the driver's double buffer handshake; the caller spins until it returns "
              "nonzero. Native: there is no buffer to hand over; ready.",
              [](Machine& m) { m.vm.Set(Reg::Eax, 1); }),
        Entry(0x28c1d, "mixer_sync_end", "Stub 10 (FUN_00028C1D): releases the handshake. Native: nothing to release.",
              [](Machine&) {}),
        Entry(0x28c4a, "sample_block_move",
              "Stub 16 (FUN_00028C4A): memmove(dst, src, n) for the game's memory manager, then fixes any "
              "channel pointing into the moved block. Native channels own a copy of their samples, so only "
              "the move itself is needed.",
              [](Machine& m) {
                  const uint32_t dst = m.Arg(0), src = m.Arg(1), n = m.Arg(2);
                  uint8_t* d = m.vm.Ptr(dst, n);
                  const uint8_t* s = m.vm.Ptr(src, n);
                  if (d && s) memmove(d, s, n);
              }, false),
    };
}

}  // namespace game::seams
