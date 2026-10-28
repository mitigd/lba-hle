// Sound seams: start/stop, the mixer ISR, volume, and music driver entry points.
#include "seams/seams.h"

namespace game::seams {

SeamList Sound() {
    return {
        {0x2a395, "cd_init", "sound",
         "FUN_00002A395(label): int 2Fh/1500h MSCDEX install check, then walks the CD drives looking "
         "for the game's volume label. Native: no CD drive (drive word = FFFFh, the original's own "
         "not-found result); music comes from the host, not the disc.",
         [](hlekit::Machine& m) {
             m.vm.Wr16(D(m, kCdDrive), 0xffff);
             m.vm.Set(hlekit::Reg::Eax, 1);
         }, 0, 0, hlekit::Seam::Ret::Near32},
        {0x2aa1e, "cd_stop", "sound",
         "FUN_00002AA1E: CD audio stop (request 85h), retried 10 times while the status has the "
         "error bit. Native: no disc, nothing is playing; the retries are skipped.",
         [](hlekit::Machine&) {}, 0, 0, hlekit::Seam::Ret::Near32},
        {0x2a570, "cd_track_length", "sound",
         "FUN_00002A570(track): length of a CD track in 75 Hz frames from the table of contents. "
         "Native: 0, as for an empty drive.",
         [](hlekit::Machine& m) { m.vm.Set(hlekit::Reg::Eax, 0); }, 0, 0, hlekit::Seam::Ret::Near32},
        {0x2aa4b, "cd_play", "sound",
         "FUN_00002AA4B(track): CD audio play of one track (request 84h), retried 10 times on error. "
         "Native: no disc, nothing starts. The music player above it falls back to its MIDI path.",
         [](hlekit::Machine&) {}, 0, 0, hlekit::Seam::Ret::Near32},
        {0x2a29c, "cd_request", "sound",
         "FUN_00002A29C: sends the request header at the DOS-memory block to MSCDEX (int 31h/0300h "
         "-> int 2Fh/1510h) and returns the status word. Native: no drive, so status = error|done, "
         "code 2 (not ready); every CD play/stop/volume wrapper above it takes its failure path.",
         [](hlekit::Machine& m) {
             m.vm.Set(hlekit::Reg::Eax, 0x8102);
         }, 0, 0, hlekit::Seam::Ret::Near32},
        // Example (remove or adapt): start digital sound.
        // {0x00000000, "sound_start", "sound",
        //  "FUN_xxxxxxxx: programs the DMA/SB and installs its mixer ISR. Native: StartSound.",
        //  [](hlekit::Machine& m) {
        //      const int rate = (int)m.vm.Get(hlekit::Reg::Eax);
        //      m.StartSound(rate, 2, 16, 4096, [](hlekit::Machine& mm) {
        //          // Queue the next guest-mixed buffer here.
        //          (void)mm;
        //      });
        //  }},
    };
}

}  // namespace game::seams
