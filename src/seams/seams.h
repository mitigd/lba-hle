// Native seam modules for Little Big Adventure (lba.exe, LE / DOS/4GW).
//
// The hardware boundary is split by area, not collected in one growing function. Every
// entry comes from docs/seam-candidates.md and is confirmed by a `[hw] unseamed hardware
// access` report or by the game's own call graph.
#pragma once

#include "hlekit/machine.h"
#include "hlekit/seam.h"

#include <chrono>
#include <cstdint>
#include <vector>

namespace game::seams {

using SeamList = hlekit::SeamList;

// Data object globals (the LE loader keeps the data object at its linked base 40000h).
constexpr uint32_t kKeyLast = 0x42821;      // last make scan code
constexpr uint32_t kKeyState0 = 0x42827;    // direction/action bit set 0
constexpr uint32_t kKeyState1 = 0x42829;    // action bit set 1 (ctrl/alt/enter/...)
constexpr uint32_t kKeyState2 = 0x42825;    // action bit set 2
constexpr uint32_t kKeyState3 = 0x42826;    // action bit set 3
constexpr uint32_t kKeyScancodes = 0x4282d; // 29 mapped scan codes
constexpr uint32_t kKeyActions = 0x4284a;   // 29 words: action type + bit mask
constexpr uint32_t kKeyExtended = 0x4288e;  // E0h prefix pending
constexpr uint32_t kKeyRaw = 0x42823;       // raw byte from port 60h
constexpr uint32_t kVideoPitch = 0x4280c;   // bytes per scan line from the mode info
constexpr uint32_t kVideoLinear = 0x42818;  // non-640 pitch flag
constexpr uint32_t kScreenBase = 0x4228e;   // guest linear address of the 640x480 screen
constexpr uint32_t kScreenWidth = 0x42280;  // 640
constexpr uint32_t kScreenHeight = 0x42284; // 480
constexpr uint32_t kVesaError = 0x4b88a;    // non-zero: no VESA extensions
constexpr uint32_t kLanguage = 0x41154;     // text language index
constexpr uint32_t kVoiceLanguage = 0x411f8; // voice language index
constexpr uint32_t kVoiceFlag = 0x411d0;     // voice files present
constexpr uint32_t kVoiceAvailable = 0x42bd4;
constexpr uint32_t kKeepVoice = 0x411dc;
constexpr uint32_t kDisplayText = 0x411e0;
constexpr uint32_t kMidiOn = 0x42bfc;        // word: music enabled
constexpr uint32_t kMidiSequence = 0x42c14;  // current sequence handle, -1 none
constexpr uint32_t kMusicVolume = 0x42c1c;   // music volume setting, 0..127
constexpr uint32_t kMidiParamNames = 0x42c04;// list of driver settings; entry 3 is the empty terminator
constexpr uint32_t kMidiParamValues = 0x5d4d8;
constexpr uint32_t kWaveOn = 0x42bd4;        // digital sound available
constexpr uint32_t kProjCenterX = 0x422b4;   // iso projection origin on screen; actors and bodies are projected through it
constexpr uint32_t kBackBuffer = 0x4c268;    // clean room background; dirty rectangles are restored from it
constexpr uint32_t kCdDrive = 0x42c32;     // MSCDEX drive number, FFFFh = none
constexpr uint32_t kTickCounter = 0x42b9c;  // 50 Hz tick counter
constexpr uint32_t kTickFrame = 0x42ba0;    // latched game frame tick
constexpr uint32_t kTickDiv = 0x42ba2;      // 50-tick divider
constexpr uint32_t kTickAccum = 0x42ba4;    // ticks since latch
constexpr uint32_t kTickAngle = 0x42ba6;    // 16-bit angle (0x5d38 per tick)
constexpr uint32_t kTickTotal = 0x42ba8;    // total timer ticks

// Data globals are decompiler/linked addresses; the loader may have moved the object.
inline uint32_t D(hlekit::Machine& m, uint32_t link) { return m.Data(link); }

// Idle loops (a menu waiting for a key) never reach the retrace wait, so they would spin the host
// CPU. The poll they do reach waits for the next 70 Hz tick here, once per call, like the
// retrace wait does; the game's own frame logic is untouched.
inline void PaceToTick(hlekit::Machine& m) {
    using namespace std::chrono;
    constexpr auto period = microseconds(14286);
    const auto n = (steady_clock::now() - m.t0) / period + 1;
    m.WaitUntil(m.t0 + n * period);
}

SeamList Video();
SeamList Input();
SeamList Sound();
SeamList Timing();
SeamList Settings();
SeamList Widescreen();
SeamList Controls();
// Save anywhere (F9): the key requests it, the main loop's key snapshot performs it, and the
// presenter draws the confirmation on the host picture.
void RequestQuickSave(hlekit::Machine& m);
void ServiceQuickSave(hlekit::Machine& m);
bool OverlayActive();
void DrawOverlay(uint8_t* picture640x480);
// Widescreen: true when config.ini asks for it; PresentWide shows the 640x480 picture with the strips.
bool WideActive(hlekit::Machine& m);
void WideFullScreen(hlekit::Machine& m);   // call on a full-screen present
bool WideInPass();                         // a strip pass is running: nothing may be presented
void RefreshPicture(hlekit::Machine& m);   // present the game's current screen again
void PresentWide(hlekit::Machine& m, const uint8_t* picture);
SeamList Music();
SeamList Samples();
// Per-tick work of the sound modules (music sequencer, mixer clock); called from the 50 Hz tick.
void ServiceMusic(hlekit::Machine& m);
void ServiceSamples(hlekit::Machine& m);
// The options the port declares in config.ini (language, ...); see settings.cpp.
std::vector<hlekit::ConfigOption> PortOptions();
SeamList All();

// Native bodies of the game's input handlers.
void OnKey(hlekit::Machine& m, uint8_t xtScan, bool down, bool extended);
void OnMouse(hlekit::Machine& m, int x, int y, uint8_t buttons);
void OnPad(hlekit::Machine& m, const hlekit::PadState& pad);

}  // namespace game::seams
