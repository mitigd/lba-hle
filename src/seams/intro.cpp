// Skip the startup intro. After loading its resources, the original's startup (FUN_00011988, the
// body of main) shows the two logo screens from ress.hqr (images 0x31/0x0c and 0x34, each held a
// few seconds by FUN_00021348), plays the DRAGON3 FLA through FUN_00025C50, loads the menu's
// background (the Adeline logo, image 0Eh), fades it in and calls the main menu (FUN_00020B18).
// This is an instruction seam at the first call of the logo sequence (the fade at 11E15h): the guest
// resumes at 11E7Eh, the background load, so the game starts at the menu. Gated by `skip_intro` in
// config.ini; when it is off the handler plays the sequence itself with the original's calls.
#include "seams/seams.h"

namespace game::seams {
namespace {

using hlekit::Machine;
using hlekit::Seam;

constexpr uint32_t kIntroStart = 0x11e15;   // push 50862h; call FUN_0001CD80 (fade out)
constexpr uint32_t kIntroEnd = 0x11e7e;     // push 0Eh: load the menu background (the Adeline logo)

constexpr uint32_t kFade = 0x1cd80, kShowImage = 0x212e0, kWait = 0x21348, kPlayFla = 0x25c50;
constexpr uint32_t kFadePalette = 0x50862, kDragon = 0x40264, kLogoFlag = 0x40e00;

// The skipped instructions, run as the original runs them (all cdecl, one argument).
void PlayIntro(Machine& m) {
    auto call = [&](uint32_t fn, uint32_t arg) { m.CallGuestCdecl(m.Data(fn), {arg}); };
    call(kFade, m.Data(kFadePalette));
    call(kShowImage, m.vm.Rd32(m.Data(kLogoFlag)) ? 0x0c : 0x31);
    call(kWait, 4);
    call(kFade, m.Data(kFadePalette));
    call(kShowImage, 0x34);
    call(kWait, 2);
    call(kFade, m.Data(kFadePalette));
    call(kPlayFla, m.Data(kDragon));
}

void SkipIntro(Machine& m) {
    if (m.Option("skip_intro") != "on") PlayIntro(m);
}

}  // namespace

SeamList Intro() {
    return {
        {kIntroStart, "skip_intro", "intro",
         "FUN_00011988 (main): the logo screens (FUN_000212E0 + FUN_00021348 waits) and the DRAGON3 "
         "FLA (FUN_00025C50), 11E15h..11E7Eh. Native: resume at 11E7Eh, the menu background load.",
         SkipIntro, kIntroEnd - kIntroStart, 0, Seam::Ret::Resume},
    };
}

}  // namespace game::seams
