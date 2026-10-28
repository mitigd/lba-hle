// Widescreen: the game's picture stays 640x480 and its UI stays where it is; the isometric room
// is drawn again, shifted, to fill the sides. AffScene (FUN_00014D58) redraws a room as: clear the
// screen, draw the brick grid (FUN_0001A3FC), copy the finished background to the back buffer (the
// call at 14DCCh), then draw the actors; later frames only restore dirty rectangles from that back
// buffer. Each brick is placed by FUN_00017237 (Xp = (x - z) * 24 + 120h, relative to the camera's
// grid cell), so drawing the grid with that 120h moved by +-N pixels shows what lies N pixels beyond
// either edge. Those passes run at the 14DCCh copy, between a kit snapshot and its restore, so the
// game's own state never sees them; the columns they produce become the side strips. Present
// composes [left strip | the game's 640x480 | right strip] and shows it at the real aspect ratio.
// The HUD, menus and dialogs are part of the 640x480 picture and stay centered, as in other
// widescreen patches.
#include "seams/seams.h"

#include <cstring>
#include <vector>

namespace game::seams {
namespace {

using hlekit::Machine;
using hlekit::Reg;
using hlekit::Seam;

constexpr int kW = 640, kH = 480;

int g_extra = -1;                       // pixels added on each side; 0 = off; -1 = not read yet
std::vector<uint8_t> g_left, g_right;   // kH rows of g_extra columns
std::vector<uint8_t> g_bg[2];           // the whole shifted room background: [0] moved right, [1] moved left
bool g_valid = false;
bool g_inPass = false;
Machine::Snapshot g_snap;

// What drawing a frame can change: the game's data object (actor records, dirty-rectangle list,
// clip window, projection globals), the screen and the back buffer. Nothing in the heap is written
// by drawing, so this is all a strip pass needs to put back - far less than a whole-memory snapshot.
struct Mini {
    struct Range { uint32_t at; std::vector<uint8_t> bytes; };
    std::vector<Range> ranges;
    void Take(Machine& m) {
        ranges.clear();
        auto add = [&](uint32_t a, uint32_t n) {
            if (const uint8_t* p = m.vm.Ptr(a, n)) ranges.push_back({a, std::vector<uint8_t>(p, p + n)});
        };
        for (auto& r : m.regions)
            if (!(r.flags & 4)) add(r.base, r.size);
        add(m.vm.Rd32(D(m, kScreenBase)), kW * kH);
        add(m.vm.Rd32(D(m, kBackBuffer)), kW * kH);
    }
    void Restore(Machine& m) {
        for (auto& r : ranges) m.vm.Write(r.at, r.bytes.data(), (uint32_t)r.bytes.size());
    }
};
Mini g_mini;

int Extra(Machine& m) {
    if (g_extra < 0) {
        const std::string s = m.Option("widescreen");
        double aspect = 0;
        if (s == "16:9") aspect = 16.0 / 9;
        else if (s == "16:10") aspect = 16.0 / 10;
        else if (s == "21:9") aspect = 21.0 / 9;
        g_extra = aspect > 0 ? ((int)(kH * aspect + 0.5) - kW) / 2 : 0;
        if (g_extra < 0) g_extra = 0;
    }
    return g_extra;
}

uint32_t Code(Machine& m, uint32_t link) { return m.Data(link); }

// The strips beyond one edge: draw the room's bricks with every brick moved `shift` pixels
// sideways and keep the columns [from, from + n) of the result. A brick's screen position is
// FUN_00017237: Xp = (x - z) * 24 + 120h, relative to the camera's grid cell, from the one `lea`
// at 17245h whose 32-bit displacement is that 120h. Its only caller is the brick drawer
// (FUN_0001A2C4), whose clip test then keeps the bricks that fall inside the 640 columns of the
// shifted picture. The displacement is changed for the pass and put back.
void Strip(Machine& m, int shift, int from, int n, std::vector<uint8_t>& out, std::vector<uint8_t>& whole) {
    auto& vm = m.vm;
    const uint32_t disp = Code(m, 0x17248);
    vm.Wr32(disp, (uint32_t)(0x120 + shift));
    m.CallGuestCdecl(Code(m, 0x26e54), {});                         // clear the screen
    m.CallGuestCdecl(Code(m, 0x1a3fc), {});                         // draw the brick grid
    vm.Wr32(disp, 0x120);
    const uint32_t base = vm.Rd32(D(m, kScreenBase));
    whole.assign(vm.Ptr(base, kW * kH), vm.Ptr(base, kW * kH) + (size_t)kW * kH);
    out.resize((size_t)n * kH);
    for (int y = 0; y < kH; ++y) {
        const uint8_t* row = vm.Ptr(base + (uint32_t)y * kW + (uint32_t)from, (uint32_t)n);
        if (row) memcpy(&out[(size_t)y * n], row, (size_t)n);
    }
}

// The instruction at 14DCCh is `call FUN_00026E66(back buffer, screen)`: copy the finished
// background into the back buffer. Native: that copy, then the strips.
void BackgroundReady(Machine& m) {
    auto& vm = m.vm;
    const uint32_t screen = vm.Rd32(D(m, kScreenBase)), back = vm.Rd32(D(m, kBackBuffer));
    uint8_t* d = vm.Ptr(back, kW * kH);
    const uint8_t* s = vm.Ptr(screen, kW * kH);
    if (d && s) memcpy(d, s, (size_t)kW * kH);

    const int n = Extra(m);
    if (!n) return;
    m.TakeSnapshot(g_snap);
    m.HoldTasks(true);
    Strip(m, +n, 0, n, g_left, g_bg[0]);
    m.RestoreSnapshot(g_snap);
    Strip(m, -n, kW - n, n, g_right, g_bg[1]);
    m.RestoreSnapshot(g_snap);
    m.HoldTasks(false);
    g_valid = true;
    m.Count("widescreen strips");
}

}  // namespace

bool WideActive(Machine& m) { return Extra(m) > 0; }
bool WideInPass() { return g_inPass; }

// A full-screen present: the strips belong to the room only while the screen still is the room.
// The back buffer holds the room's clean background; menus and actors cover a small part of it,
// while a full-screen picture (holomap, story, main menu) matches almost none of it.
void WideFullScreen(Machine& m) {
    if (!g_valid || g_inPass) return;
    auto& vm = m.vm;
    const uint8_t* s = vm.Ptr(vm.Rd32(D(m, kScreenBase)), kW * kH);
    const uint8_t* b = vm.Ptr(vm.Rd32(D(m, kBackBuffer)), kW * kH);
    if (!s || !b) { g_valid = false; return; }
    size_t same = 0;
    for (size_t i = 0; i < (size_t)kW * kH; ++i) same += s[i] == b[i];
    g_valid = same * 100 > (size_t)kW * kH * 60;
}

void PresentWide(Machine& m, const uint8_t* picture) {
    const int n = Extra(m), w = kW + 2 * n;
    static std::vector<uint8_t> wide;
    wide.assign((size_t)w * kH, 0);
    for (int y = 0; y < kH; ++y) {
        uint8_t* row = &wide[(size_t)y * w];
        if (g_valid) {
            memcpy(row, &g_left[(size_t)y * n], (size_t)n);
            memcpy(row + n + kW, &g_right[(size_t)y * n], (size_t)n);
        }
        memcpy(row + n, picture + (size_t)y * kW, kW);
    }
    m.PresentFrame(wide.data(), w, kH, (double)w / kH);
}

namespace {

// Each frame the original draws its actors over the 640 columns. The actors that stand in the
// strips are drawn by running AffScene's per-frame path again over the shifted background of each
// side: the dirty rectangles are restored from the back buffer, then every actor is projected
// through XCenter (moved by the same amount) and culled against the 640 columns of the shifted
// picture, so only those beyond the edge land in the captured strip. Nothing is presented while
// this runs and the snapshot puts the guest back exactly as the real frame left it.
void ActorStrips(Machine& m) {
    const int n = Extra(m);
    if (!n || !g_valid || g_bg[0].empty() || g_bg[1].empty()) return;
    auto& vm = m.vm;
    g_inPass = true;
    g_mini.Take(m);
    m.HoldTasks(true);
    for (int side = 0; side < 2; ++side) {
        const int shift = side == 0 ? +n : -n;
        const uint32_t screen = vm.Rd32(D(m, kScreenBase)), back = vm.Rd32(D(m, kBackBuffer));
        if (uint8_t* d = vm.Ptr(screen, kW * kH)) memcpy(d, g_bg[side].data(), (size_t)kW * kH);
        if (uint8_t* d = vm.Ptr(back, kW * kH)) memcpy(d, g_bg[side].data(), (size_t)kW * kH);
        vm.Wr32(D(m, kProjCenterX), (uint32_t)((int32_t)vm.Rd32(D(m, kProjCenterX)) + shift));
        m.CallOriginal(Code(m, 0x14d58), {0, 0, 0, 0, 0});
        const int from = side == 0 ? 0 : kW - n;
        std::vector<uint8_t>& out = side == 0 ? g_left : g_right;
        for (int y = 0; y < kH; ++y) {
            const uint8_t* row = vm.Ptr(screen + (uint32_t)y * kW + (uint32_t)from, (uint32_t)n);
            if (row) memcpy(&out[(size_t)y * n], row, (size_t)n);
        }
        g_mini.Restore(m);
    }
    m.HoldTasks(false);
    g_inPass = false;
}

// FUN_00014D58(full): the room frame. Native: the original, then the actors in the strips.
void SceneFrame(Machine& m) {
    // The original room loop is not paced at all - on a 486 the drawing itself was the limit. The
    // game's logic runs from its 50 Hz tick, so one frame per 70 Hz tick is all anyone can see.
    if (!g_inPass) PaceToTick(m);
    const uint32_t full = m.Arg(0);
    const uint32_t r = m.CallOriginal(Code(m, 0x14d58), {0, 0, 0, 0, full});
    m.vm.Set(Reg::Eax, r);
    if (!g_inPass && WideActive(m) && g_valid) {
        ActorStrips(m);
        RefreshPicture(m);
    }
}

}  // namespace

SeamList Widescreen() {
    return {
        {0x14d58, "scene_frame", "video",
         "FUN_00014D58(full): draws one frame of the isometric room - on a full redraw the background, "
         "then every actor, restoring dirty rectangles from the back buffer on the other frames. Native: "
         "paced to one frame per 70 Hz tick (the original loop is not paced), then the original, then "
         "(widescreen) the same per-frame path over the shifted backgrounds to draw the actors that "
         "stand in the side strips.",
         SceneFrame, 0, 0, Seam::Ret::Near32},
        {0x14dcc, "scene_background_ready", "video",
         "The call to FUN_00026E66 inside AffScene (FUN_00014D58) that copies the finished room "
         "background from the screen into the back buffer. Native: that copy, then the room's bricks "
         "drawn twice more with the brick origin (the 120h in FUN_00017237) moved by +-N pixels, between a "
         "snapshot and its restore, to make the left and right strips of the widescreen picture.",
         BackgroundReady, 5, 0, Seam::Ret::Resume},
    };
}

}  // namespace game::seams
