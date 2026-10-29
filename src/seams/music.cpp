// Music seams: the game's XMIDI player. The original registers an XMIDI sequence with the Miles
// AIL music driver, installs the FM timbres it asks for, and starts it; the driver then plays it
// from the 50 Hz timer into the sound card. Native: the kit's XMIDI sequencer on the host General
// MIDI synthesizer (the SoundFont in config.ini). The game's own functions are what is replaced:
// FUN_00029FB0 (play), FUN_0002A0E0 (stop), FUN_0002A130 (playing?), FUN_0002A160 (fade out),
// FUN_0002A220 (volume), FUN_0002A260 (restart when done), plus the init steps that load a
// driver file and start the device. No driver, no timbre bank: General MIDI needs neither.
#include "seams/seams.h"

#include "hlekit/midi.h"

#include <algorithm>
#include <atomic>
#include <memory>
#include <mutex>

namespace game::seams {
namespace {

using hlekit::Machine;
using hlekit::Reg;
using hlekit::Seam;
using hlekit::XmiPlayer;

// Without a host synthesizer (headless, muted, no SoundFont) the sequencer still runs on host
// time, so the game sees sequences start, loop and finish as it would; notes are only counted.
class CountingMidi : public hlekit::MidiOut {
public:
    explicit CountingMidi(Machine& m) : m_(m) {}
    void Short(uint8_t status, uint8_t, uint8_t d2) override {
        if ((status & 0xf0) == 0x90 && d2) ++notes_;
    }
    // Machine::Count is not thread-safe; the sequencer runs on the audio thread, so notes are
    // tallied here and folded in from the guest thread.
    void Flush() { for (uint32_t n = notes_.exchange(0); n; --n) m_.Count("midi note on"); }
    void Sysex(const uint8_t*, size_t) override {}
    void Reset() override {}
private:
    Machine& m_;
    std::atomic<uint32_t> notes_{0};
};

std::unique_ptr<CountingMidi> g_nullMidi;
std::mutex g_musicMu;         // the sequencer is stepped from the audio thread
std::unique_ptr<XmiPlayer> g_xmi;
int g_percent = 100;          // the driver's master volume as a percent of each sequence's own

double Now(Machine& m) { return std::chrono::duration<double>(std::chrono::steady_clock::now() - m.t0).count(); }

bool MusicOn(Machine& m) { return g_xmi && m.vm.Rd16(D(m, kMidiOn)) != 0; }

int32_t Current(Machine& m) { return (int32_t)m.vm.Rd32(D(m, kMidiSequence)); }

void Release(Machine& m) {
    const int32_t h = Current(m);
    if (h == -1) return;
    g_xmi->Stop(h);
    g_xmi->Release(h);
    m.vm.Wr32(D(m, kMidiSequence), 0xffffffff);
}

// The empty entry that ends the game's own "driver parameters" list (MidiBase/IRQ/DMA are sound
// card settings; a native device has none).
void NoParameters(Machine& m) {
    m.vm.Wr32(m.Arg(0), D(m, kMidiParamNames) + 12);
    m.vm.Wr32(m.Arg(1), D(m, kMidiParamValues));
}

void Play(Machine& m) {
    if (!MusicOn(m)) return;
    Release(m);
    const uint32_t at = m.Arg(0);
    const uint8_t* data = nullptr;
    size_t avail = 0;
    for (size_t len : {size_t(1) << 20, size_t(1) << 19, size_t(1) << 18, size_t(1) << 16}) {
        if ((data = m.vm.Ptr(at, (uint32_t)len))) { avail = len; break; }
    }
    if (!data) return;
    const size_t size = XmiPlayer::FileSize(data, avail);
    if (!size) return;
    const int h = g_xmi->Register(data, size, 0);
    if (h < 0) return;
    m.vm.Wr32(D(m, kMidiSequence), (uint32_t)h);
    g_xmi->RelativeVolume(h, g_percent, 0);
    g_xmi->Start(h);
    m.Count("music start");
}

}  // namespace

void ServeMusicThread(Machine& m) {
    std::lock_guard<std::mutex> lock(g_musicMu);
    if (g_xmi) g_xmi->Serve(Now(m));
}

void ServiceMusic(Machine& m) {
    if (g_nullMidi) g_nullMidi->Flush();
    if (g_xmi && !g_audioThread) {
        std::lock_guard<std::mutex> lock(g_musicMu);
        g_xmi->Serve(Now(m));
    }
}

SeamList Music() {
    SeamList list = {
        {0x29c00, "midi_driver_load", "sound",
         "FUN_00029C00(file): loads the AIL XMIDI driver DLL named in LBA.CFG and asks it to find the "
         "card. Native: no driver file; always succeeds.",
         [](Machine& m) { m.vm.Set(Reg::Eax, 1); }, 0, 0, Seam::Ret::Near32},
        {0x29b80, "midi_driver_params", "sound",
         "FUN_00029B80(&names, &values): the list of card settings (MidiBase, MidiIRQ, MidiDMA) the "
         "driver needs from LBA.CFG. Native: the list's terminating empty entry, i.e. none needed.",
         NoParameters, 0, 0, Seam::Ret::Near32},
        {0x29d50, "midi_start", "sound",
         "FUN_00029D50: initializes the driver with the card settings, sets the music-enabled word at "
         "42BFCh, allocates the AIL state table and reads the FM timbre bank. Native: enable music "
         "and create the XMIDI sequencer on the host synthesizer (General MIDI needs no timbres).",
         [](Machine& m) {
             if (!g_xmi) {
                 hlekit::MidiOut* out = m.midi.get();
                 if (!out) {
                     g_nullMidi = std::make_unique<CountingMidi>(m);
                     out = g_nullMidi.get();
                 }
                 g_xmi = std::make_unique<XmiPlayer>(out);
             }
             m.vm.Wr16(D(m, kMidiOn), 1);
             m.vm.Wr32(D(m, kMidiSequence), 0xffffffff);
             m.vm.Set(Reg::Eax, 1);
         }, 0, 0, Seam::Ret::Near32},
        {0x29fb0, "midi_play", "sound",
         "FUN_00029FB0(xmi): stops the current sequence, registers the XMIDI data, installs the "
         "timbres it requests and starts it. Native: register it with the sequencer and start it.",
         Play, 0, 0, Seam::Ret::Near32},
        {0x2a0e0, "midi_stop", "sound",
         "FUN_0002A0E0: stops and releases the current sequence handle (42C14h).",
         [](Machine& m) { if (MusicOn(m)) Release(m); }, 0, 0, Seam::Ret::Near32},
        {0x2a130, "midi_playing", "sound",
         "FUN_0002A130: 1 while the current sequence's status is playing, else 0.",
         [](Machine& m) {
             const int32_t h = MusicOn(m) ? Current(m) : -1;
             m.vm.Set(Reg::Eax, h != -1 && g_xmi->StatusOf(h) == XmiPlayer::kPlaying ? 1 : 0);
         }, 0, 0, Seam::Ret::Near32},
        {0x2a160, "midi_fade_out", "sound",
         "FUN_0002A160(seconds): ramps the driver's master volume to 0 over seconds * 1000 ms.",
         [](Machine& m) {
             const int32_t h = MusicOn(m) ? Current(m) : -1;
             if (h != -1) g_xmi->RelativeVolume(h, 0, (int16_t)m.Arg(0) * 1000);
             g_percent = 0;
         }, 0, 0, Seam::Ret::Near32},
        {0x2a220, "midi_volume", "sound",
         "FUN_0002A220(percent): master volume = percent * the music volume setting (42C1Ch, 0..127) "
         "/ 100, set at once. Native: the same as a percent of full level.",
         [](Machine& m) {
             const int vol = (int16_t)m.Arg(0) * (int)m.vm.Rd32(D(m, kMusicVolume)) / 100;
             g_percent = std::clamp(vol * 100 / 127, 0, 100);
             const int32_t h = MusicOn(m) ? Current(m) : -1;
             if (h != -1) g_xmi->RelativeVolume(h, g_percent, 0);
         }, 0, 0, Seam::Ret::Near32},
        {0x2a260, "midi_loop_if_done", "sound",
         "FUN_0002A260: when the sequence has finished (status 2) starts it again.",
         [](Machine& m) {
             const int32_t h = MusicOn(m) ? Current(m) : -1;
             if (h != -1 && g_xmi->StatusOf(h) == XmiPlayer::kDone) g_xmi->Start(h);
         }, 0, 0, Seam::Ret::Near32},
        {0x29f80, "midi_shutdown", "sound",
         "FUN_00029F80: stops the sequence, shuts the driver down and clears the enabled word.",
         [](Machine& m) {
             if (g_xmi) {
                 if (MusicOn(m)) Release(m);
                 g_xmi->StopAll();
             }
             m.vm.Wr16(D(m, kMidiOn), 0);
         }, 0, 0, Seam::Ret::Near32},
    };
    for (Seam& seam : list) {
        seam.fn = [fn = std::move(seam.fn)](Machine& m) {
            std::lock_guard<std::mutex> lock(g_musicMu);
            fn(m);
        };
    }
    return list;
}

}  // namespace game::seams
