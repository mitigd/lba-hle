// Save anywhere. The original saves by itself: FUN_0001EDAC writes the player's save file
// (sNNNN.LBA: the name, the 255 game flags, the inventory, the scene, life, money, behaviour...) at
// each room change, and the file records where the room is entered, not where Twinsen stands.
// The position the loader restores is three words (13C29Ah x, 13C296h y, 13C292h z; the main loop
// copies them into the hero at 11392h), and his heading is his live angle. So a save where he
// stands is the original's writer with those words set to his position for the call - everything
// else it writes is live state. Press F9 (the key is unused by the game); a short message is drawn
// on the host picture, never into the game's screen buffer. The request is honoured at the main
// loop's key snapshot, a point where the game is between frames, and only while Twinsen is under
// the player's control.
#include "seams/seams.h"

#include "hlekit/font.h"

#include <algorithm>
#include <string>

namespace game::seams {
namespace {

using hlekit::Machine;

constexpr uint32_t kHero = 0x4d4f4;                 // actor 0 (Twinsen)
constexpr uint32_t kStartX = 0x4c29a, kStartY = 0x4c296, kStartZ = 0x4c292;
constexpr uint32_t kSaveWriter = 0x1edac;

bool g_requested = false;
double g_requestedAt = 0;
std::string g_message;
double g_messageUntil = 0;
uint8_t g_ink = 255, g_shade = 0;

bool Enabled(Machine& m) {
    static int on = -1;
    if (on < 0) on = m.Option("save_anywhere") == "on";
    return on != 0;
}

void Say(Machine& m, const std::string& text) {
    g_message = text;
    g_messageUntil = m.Seconds() + 2.0;
    int best = -1, worst = 1 << 30, lo = 0, hi = 0;   // the brightest and darkest palette entries
    for (int i = 0; i < 256; ++i) {
        const int v = m.palette[i * 3] + m.palette[i * 3 + 1] + m.palette[i * 3 + 2];
        if (v > best) { best = v; hi = i; }
        if (v < worst) { worst = v; lo = i; }
    }
    g_ink = (uint8_t)hi;
    g_shade = (uint8_t)lo;
}

void SaveHere(Machine& m) {
    auto& vm = m.vm;
    const uint32_t hero = D(m, kHero);
    const bool control = vm.Rd16(hero + 0x40) == 1 && (int16_t)vm.Rd16(hero + 0x18) != -1;
    if (!control) { Say(m, "Cannot save now"); return; }
    const uint32_t x = D(m, kStartX), y = D(m, kStartY), z = D(m, kStartZ);
    const uint16_t keep[3] = {vm.Rd16(x), vm.Rd16(y), vm.Rd16(z)};
    vm.Wr16(x, vm.Rd16(hero + 0x1a));
    vm.Wr16(y, vm.Rd16(hero + 0x1c));
    vm.Wr16(z, vm.Rd16(hero + 0x1e));
    m.CallGuestCdecl(m.Data(kSaveWriter), {});
    vm.Wr16(x, keep[0]);
    vm.Wr16(y, keep[1]);
    vm.Wr16(z, keep[2]);
    Say(m, "Game saved");
    m.Count("quick save");
}

}  // namespace

void RequestQuickSave(Machine& m) {
    if (!Enabled(m)) return;
    g_requested = true;
    g_requestedAt = m.Seconds();
}

// Called every main-loop frame (the key snapshot).
void ServiceQuickSave(Machine& m) {
    const double now = m.Seconds();
    if (g_requested) {
        g_requested = false;
        if (now - g_requestedAt < 0.5) SaveHere(m);     // a press inside a menu must not fire later
    }
    if (!g_message.empty() && now > g_messageUntil) {
        g_message.clear();
        RefreshPicture(m);
    }
}

bool OverlayActive() { return !g_message.empty(); }

void DrawOverlay(uint8_t* picture) {
    static const hlekit::Font font = hlekit::Font::Builtin(16);
    const int h = font.height(), w = (int)g_message.size() * 8;
    const int x0 = (640 - w) / 2, y0 = 10;
    for (int pass = 0; pass < 2; ++pass) {              // a one-pixel shade first, then the ink
        const int dx = pass == 0 ? 1 : 0, dy = pass == 0 ? 1 : 0;
        const uint8_t color = pass == 0 ? g_shade : g_ink;
        for (size_t c = 0; c < g_message.size(); ++c) {
            const uint8_t* rows = font.Glyph((uint8_t)g_message[c]);
            for (int r = 0; r < h; ++r)
                for (int b = 0; b < 8; ++b)
                    if (rows[r] & (0x80 >> b)) {
                        const int x = x0 + (int)c * 8 + b + dx, y = y0 + r + dy;
                        if (x >= 0 && x < 640 && y >= 0 && y < 480) picture[y * 640 + x] = color;
                    }
        }
    }
}

}  // namespace game::seams
