// Direct controls: the arrow keys move Twinsen across the screen instead of turning him. The
// original is a tank scheme - Left/Right turn, Up walks forward, Down walks backward - and keeps
// working when `controls = classic` in config.ini. With `direct`, the key snapshot the main loop
// takes (the move at 108C7h: key state 427h -> 130DE0h, which the hero's control code then reads
// bit by bit) is replaced: the pressed arrows name a direction on the screen, Twinsen is turned to
// it by stepping his stored heading a little each frame, and once he faces it the game is shown a
// plain "Up". Everything after that - the walk and run
// animations, the behaviours, collisions - is the original, unchanged.
//
// The screen direction becomes a heading through the isometric projection (FUN_00026BE7):
// Xp = (x - z) * 24 / 512 and Yp = (x + z) * 12 / 512, so the world direction of a screen step
// (sx, sy) is (dx, dz) ~ (sx + 2 sy, 2 sy - sx), and angles are 1024 to a turn with x = sin, z = cos.
#include "seams/seams.h"

#include <algorithm>
#include <cmath>

namespace game::seams {
namespace {

using hlekit::Machine;
using hlekit::Reg;
using hlekit::Seam;

constexpr uint32_t kHero = 0x4d4f4;          // actor 0 (Twinsen); records are 7Ch long
constexpr int kStep = 60;                    // heading change per frame, 1024 to a turn
constexpr int kBody = 0x18, kAngle = 0x32, kTurn = 0x36, kMode = 0x40, kFlags = 0x63;

int g_direct = -1;

bool Direct(Machine& m) {
    if (g_direct < 0) g_direct = m.Option("controls") == "direct";
    return g_direct != 0;
}

// The key snapshot. `ax` holds key state 427h: bit 0 up, 1 down, 2 left, 3 right (the keypad
// diagonals set two).
void Snapshot(Machine& m) {
    auto& vm = m.vm;
    uint16_t keys = (uint16_t)vm.Get(Reg::Eax);
    const uint32_t hero = D(m, kHero);
    const bool manual = vm.Rd16(hero + kMode) == 1 && (int16_t)vm.Rd16(hero + kBody) != -1 &&
                        !(vm.Rd8(hero + kFlags) & 1);
    if (Direct(m) && manual && (keys & 0x0f)) {
        const int sx = ((keys & 8) ? 1 : 0) - ((keys & 4) ? 1 : 0);
        const int sy = ((keys & 2) ? 1 : 0) - ((keys & 1) ? 1 : 0);
        keys &= ~0x0f;
        if (sx || sy) {
            const double dx = sx + 2 * sy, dz = 2 * sy - sx;
            int want = (int)std::lround(std::atan2(dx, dz) * 1024.0 / (2 * M_PI)) & 0x3ff;
            // The game re-initialises its rotation state from the stored angle every frame, so a
            // timed rotation never progresses. Drive the stored angle (+32h) directly, a fixed step
            // per frame (a half turn takes about 9 frames), and keep the rotation state at rest on it.
            const uint32_t state = hero + kTurn;
            const int now = (int16_t)vm.Rd16(hero + kAngle) & 0x3ff;
            const int off = (((want - now) & 0x3ff) + 512) % 1024 - 512;
            const int step = std::max(-kStep, std::min(kStep, off));
            const int next = (now + step) & 0x3ff;
            vm.Wr16(hero + kAngle, (uint16_t)next);
            vm.Wr16(state, (uint16_t)next);
            vm.Wr16(state + 2, (uint16_t)next);
            vm.Wr16(state + 4, 0);
            if (std::abs(off - step) <= 40) keys |= 1;   // facing it: walk
        }
    }
    vm.Wr16(D(m, 0x40de0), keys);
}

}  // namespace

SeamList Controls() {
    return {
        {0x108c7, "key_snapshot", "input",
         "The move at 108C7h in the main loop: key state 427h into the control copy at 130DE0h that the "
         "hero's control code reads. Native: that copy, and with controls = direct the arrows are "
         "turned into a screen heading, the hero is rotated to it with the game's own rotation, and "
         "a plain Up is presented once he faces it.",
         Snapshot, 6, 0, Seam::Ret::Resume},
    };
}

}  // namespace game::seams
