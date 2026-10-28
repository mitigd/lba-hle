// Timing seams: the game's 50 Hz timer. FUN_0002F4E0 installs FUN_000283CA on vector 8 and
// programs PIT channel 0 with reload 5D38h; the ISR advances the game clock and, every 50
// ticks, latches the frame counter the main loop watches. Native: a host 50 Hz timer runs the
// same guest-data updates. No PIT or PIC is modeled.
#include "seams/seams.h"

namespace game::seams {
namespace {

using hlekit::Machine;
using hlekit::Seam;

void Tick(Machine& m) {
    auto& vm = m.vm;
    vm.Wr32(D(m, kTickTotal), vm.Rd32(D(m, kTickTotal)) + 1);
    vm.Wr32(D(m, kTickCounter), vm.Rd32(D(m, kTickCounter)) + 1);
    uint16_t div = (uint16_t)(vm.Rd16(D(m, kTickDiv)) - 1);
    if (div == 0) {
        vm.Wr16(D(m, kTickDiv), 50);
        vm.Wr16(D(m, kTickFrame), vm.Rd16(D(m, kTickAccum)));
        vm.Wr16(D(m, kTickAccum), 0);
    } else {
        vm.Wr16(D(m, kTickDiv), div);
    }
    vm.Wr16(D(m, kTickAngle), (uint16_t)(vm.Rd16(D(m, kTickAngle)) + 0x5d38));
    ServiceMusic(m);
    ServiceSamples(m);
}

// FUN_00028432, the callback the AIL timer service runs at 50 Hz when music is on: the same clock
// updates as the ISR, without the BIOS-tick chaining. It is not a hardware interrupt body.
void TickCallback(Machine& m) {
    auto& vm = m.vm;
    vm.Wr32(D(m, kTickCounter), vm.Rd32(D(m, kTickCounter)) + 1);
    vm.Wr32(D(m, kTickTotal), vm.Rd32(D(m, kTickTotal)) + 1);
    uint16_t div = (uint16_t)(vm.Rd16(D(m, kTickDiv)) - 1);
    if (div == 0) {
        vm.Wr16(D(m, kTickDiv), 50);
        vm.Wr16(D(m, kTickFrame), vm.Rd16(D(m, kTickAccum)));
        vm.Wr16(D(m, kTickAccum), 0);
    } else {
        vm.Wr16(D(m, kTickDiv), div);
    }
    ServiceMusic(m);
    ServiceSamples(m);
}

double g_timerHz = 0;

}  // namespace

SeamList Timing() {
    return {
        {0x2f4e0, "install_timer", "timing",
         "FUN_00002F4E0: saves/sets vector 8 to FUN_000283CA, then programs PIT ch0 with reload "
         "5D38h (50 Hz). Native: the guest ISR body on a host 50 Hz timer.",
         [](Machine& m) { m.SetTimer(50.0, Tick); }, 0, 0, Seam::Ret::Near32},
        {0x2fdc6, "ail_timer_register", "timing",
         "FUN_0002FDC6(callback): AIL_register_timer - takes a slot in the AIL timer table, and for the "
         "first one programs the PIT. Native: the callback is the game's tick (FUN_00028432), which the "
         "host timer runs natively; handle 0.",
         [](Machine& m) { m.vm.Set(hlekit::Reg::Eax, 0); }, 0, 0, Seam::Ret::Near32},
        {0x2fff7, "ail_timer_rate", "timing",
         "FUN_0002FFF7(timer, hz): AIL_set_timer_frequency. Native: remembered for the start.",
         [](Machine& m) { g_timerHz = (double)m.Arg(1); }, 0, 0, Seam::Ret::Near32},
        {0x2feee, "ail_timer_start", "timing",
         "FUN_0002FEEE(timer): AIL_start_timer. Native: the host timer at the requested rate.",
         [](Machine& m) { m.SetTimer(g_timerHz > 0 ? g_timerHz : 50.0, TickCallback); }, 0, 0, Seam::Ret::Near32},
        {0x2f568, "remove_timer", "timing",
         "FUN_00002F568: restores the saved vector 8 and PIT ch0. Native: stop the host timer.",
         [](Machine& m) { m.SetTimer(0, nullptr); }, 0, 0, Seam::Ret::Near32},
        {0x283b4, "pit_rate", "timing",
         "FUN_0000283B4(reload): out 43h,36h; out 40h,lo; out 40h,hi. Native: accepted, the host "
         "timer is owned by install_timer.",
         [](Machine&) {}, 0, 0, Seam::Ret::Near32},
        {0x2fbf8, "ail_pit_rate", "timing",
         "FUN_00002FBF8(reload): AIL timer PIT ch0 programming. Native: accepted; the sound timer "
         "is driven by host audio when its driver is seamed.",
         [](Machine&) {}, 0, 0, Seam::Ret::Near32},
    };
}

}  // namespace game::seams
