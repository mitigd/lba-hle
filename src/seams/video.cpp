// Video seams: the VGA DAC, the VESA mode the built-in driver selects, the retrace wait,
// and the game's banked screen copy. The game's own renderers stay guest code: they fill the
// 640x480 off-screen buffer at [4228Eh], and the seam below presents each finished rectangle.
#include "seams/seams.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <mutex>
#include <vector>

namespace game::seams {
namespace {

using hlekit::Machine;
using hlekit::Reg;
using hlekit::Seam;

constexpr uint32_t kPaletteScratch = 0x42890;

uint32_t Arg(Machine& m, int n) { return m.Arg(n); }

uint8_t Dac(uint8_t v) { return (uint8_t)(v >> 2); }

void SetPalette(Machine& m) {
    const uint32_t src = Arg(m, 0);
    const uint8_t* p = m.vm.Ptr(src, 768);
    if (!p) return;
    std::lock_guard<std::mutex> lock(m.paletteMutex);
    for (int i = 0; i < 768; ++i) m.palette[i] = Dac(p[i]);
}

void SetPaletteRange(Machine& m) {
    const uint32_t start = Arg(m, 0);
    const uint32_t count = Arg(m, 1);
    const uint32_t src = Arg(m, 2);
    const uint8_t* p = m.vm.Ptr(src, count * 3);
    if (!p) return;
    std::lock_guard<std::mutex> lock(m.paletteMutex);
    for (uint32_t i = 0; i < count * 3; ++i) {
        const uint32_t at = start * 3 + i;
        if (at < 768) m.palette[at] = Dac(p[i]);
    }
}

void SetPaletteEntry(Machine& m) {
    const uint32_t start = Arg(m, 0), r = Arg(m, 1), g = Arg(m, 2), b = Arg(m, 3);
    if (start * 3 + 2 >= 768) return;
    std::lock_guard<std::mutex> lock(m.paletteMutex);
    m.palette[start * 3] = Dac((uint8_t)r);
    m.palette[start * 3 + 1] = Dac((uint8_t)g);
    m.palette[start * 3 + 2] = Dac((uint8_t)b);
}

// FUN_0002F323: the built-in VESA driver asks the BIOS for mode 101h (640x480x8), reads the
// pitch from the mode-info block, and patches its bank switch. Native: the mode is the host
// picture; there is no VBE call and no bank window. The game's 640-wide pitch is what its
// line table and screen copy already use.
void SelectVesa(Machine& m) {
    m.vm.Wr8(D(m, kVesaError), 0);
    m.vm.Wr32(D(m, kVideoPitch), 640);
    m.vm.Wr8(D(m, kVideoLinear), 0);
    m.videoMode = 0x101;
    // The game calls the driver's bank switch from its screen copy. The native copy seam does
    // not need it; keep the pointer harmless for any other caller.
    static uint32_t bank = 0;
    if (!bank) {
        Seam noop{0, "vesa_bank_noop", "video",
                  "VESA window select from the built-in driver; native screen copy has no banks.", [](Machine&) {},
                  0, 0, Seam::Ret::Near32};
        bank = m.NativeFunction(noop);
    }
    m.vm.Wr32(D(m, 0x422a6), bank);
}

// The original's banked VRAM copies were slow enough to pace its menu redraw loops; native
// copies are not, so a loop that redraws without waiting for retrace would present thousands of
// times a second. A tick (70 Hz) may present up to kAreaPerTick pixels - a few full screens, as
// menus do - and past that the caller sleeps to the next tick. Small rects (the intro text
// draws one per glyph) are cheap and cost by area; kPresentsPerTick only backstops a spin loop of
// tiny ones. Game logic is unaffected.
void LimitPresents(Machine& m, int area) {
    constexpr int kAreaPerTick = 4 * 640 * 480;
    constexpr int kPresentsPerTick = 512;
    constexpr auto period = std::chrono::microseconds(14286);
    static int64_t tick = -1;
    static int used = 0, count = 0;
    const int64_t now = (std::chrono::steady_clock::now() - m.t0) / period;
    if (now != tick) { tick = now; used = 0; count = 0; }
    used += area;
    ++count;
    if (used <= kAreaPerTick && count <= kPresentsPerTick) return;
    PaceToTick(m);
    tick = (std::chrono::steady_clock::now() - m.t0) / period;
    used = area;
    count = 1;
}

// Copies a rectangle (inclusive corners) of the game's screen buffer to the host 640x480
// picture and presents it. This is what every banked A0000h copy in the original does.
void Present(Machine& m, int x1, int y1, int x2, int y2) {
    if (WideInPass()) return;   // a widescreen strip pass is drawing; the real frame is presented
    if (x2 < x1 || y2 < y1 || x1 < 0 || y1 < 0 || x2 > 639 || y2 > 479) return;
    uint32_t pitch = m.vm.Rd32(D(m, kVideoPitch));
    if (!pitch) pitch = m.vm.Rd32(D(m, kScreenWidth));
    if (!pitch) pitch = 640;
    const uint32_t base = m.vm.Rd32(D(m, kScreenBase));
    const int w = x2 - x1 + 1, h = y2 - y1 + 1;
    static std::vector<uint8_t> frame(640 * 480);
    for (int y = 0; y < h; ++y) {
        const uint8_t* src = m.vm.Ptr(base + (uint32_t)(y1 + y) * pitch + (uint32_t)x1, (uint32_t)w);
        if (!src) return;
        memcpy(&frame[(size_t)(y1 + y) * 640 + x1], src, (size_t)w);
    }
    LimitPresents(m, w * h);
    const uint8_t* shown = frame.data();
    static std::vector<uint8_t> withMessage;
    if (OverlayActive()) {                       // the save confirmation, on the host picture only
        withMessage = frame;
        DrawOverlay(withMessage.data());
        shown = withMessage.data();
    }
    if (WideActive(m)) PresentWide(m, shown);
    else m.PresentFrame(shown, 640, 480, 4.0 / 3.0);
}

// FUN_0002701A(x1, y1, x2, y2): copies a rectangle from the game's screen buffer to the
// banked A0000h window, switching VESA banks as it crosses 64 KB. Native: present it.
void PresentRect(Machine& m) {
    Present(m, (int)Arg(m, 0), (int)Arg(m, 1), (int)Arg(m, 2), (int)Arg(m, 3));
}

// FUN_00026E8F(x1, y1, x2, y2): the same copy, first clipped to the game's clip window
// (minX 4135Ch, minY 41360h, maxX 41364h, maxY 41368h); nothing is copied if it is outside.
void PresentClipped(Machine& m) {
    int x1 = (int)Arg(m, 0), y1 = (int)Arg(m, 1), x2 = (int)Arg(m, 2), y2 = (int)Arg(m, 3);
    const int minX = (int)m.vm.Rd32(D(m, 0x4135c)), minY = (int)m.vm.Rd32(D(m, 0x41360));
    const int maxX = (int)m.vm.Rd32(D(m, 0x41364)), maxY = (int)m.vm.Rd32(D(m, 0x41368));
    if (x1 > maxX || x2 < minX || y1 > maxY || y2 < minY) return;
    Present(m, std::max(x1, minX), std::max(y1, minY), std::min(x2, maxX), std::min(y2, maxY));
}

// FUN_000272CD(): the whole 640x480 screen buffer to the banked window, five bank switches.
void PresentAll(Machine& m) {
    WideFullScreen(m);
    Present(m, 0, 0, 639, 479);
}

}  // namespace

void RefreshPicture(Machine& m) { Present(m, 0, 0, 639, 479); }

SeamList Video() {
    using namespace std::chrono;
    return {
        {0x27fd4, "set_palette", "video",
         "FUN_000027FD4(ptr): 256 RGB triples, each >> 2, to DAC index 0. Native: Machine::palette.",
         SetPalette, 0, 0, Seam::Ret::Near32},
        {0x28028, "set_palette_range", "video",
         "FUN_000028028(start, count, ptr): count RGB triples, each >> 2, to DAC index start.",
         SetPaletteRange, 0, 0, Seam::Ret::Near32},
        {0x2807b, "set_palette_entry", "video",
         "FUN_00002807B(index, r, g, b): one DAC entry, components >> 2. Native: Machine::palette.",
         SetPaletteEntry, 0, 0, Seam::Ret::Near32},
        {0x26e7c, "wait_retrace", "video",
         "FUN_000026E7C: polls VGA status port 3DAh for the end and start of retrace. Native: 70 Hz "
         "host pacing; the game's own frame logic stays guest code.",
         [](Machine& m) {
             constexpr auto period = microseconds(14286);
             const auto n = (steady_clock::now() - m.t0) / period + 1;
             m.WaitUntil(m.t0 + n * period);
             ++m.frames;
         }, 0, 0, Seam::Ret::Near32},
        {0x2f323, "vesa_mode", "video",
         "FUN_00002F323: built-in VESA driver init, int 10h/4F01h + 4F02h for mode 101h (640x480x8), "
         "reads the pitch and patches the bank switch. Native: select the 640x480 host picture.",
         SelectVesa, 0, 0, Seam::Ret::Near32},
        {0x2701a, "present_rect", "video",
         "FUN_00002701A(x1,y1,x2,y2): copies a rectangle from the game's 640x480 screen buffer to "
         "the banked A0000h window, calling the VESA window select at each 64 KB crossing. Native: "
         "copy the same rectangle to the host picture and present it.",
         PresentRect, 0, 0, Seam::Ret::Near32},
        {0x26e8f, "present_clipped", "video",
         "FUN_00026E8F(x1,y1,x2,y2): clips to the game's clip window, then the same banked copy to "
         "A0000h as FUN_0002701A. Native: clip, copy to the host picture, present.",
         PresentClipped, 0, 0, Seam::Ret::Near32},
        {0x272cd, "present_all", "video",
         "FUN_000272CD: copies the whole 640x480 screen buffer to A0000h, 4000h dwords per 64 KB "
         "bank, switching banks 0..4 (the linear-pitch case calls FUN_0002701A). Native: present it.",
         PresentAll, 0, 0, Seam::Ret::Near32},
    };
}

}  // namespace game::seams
