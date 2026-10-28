// Input seams: the game's keyboard ISR and its BIOS key reader. Host keys are delivered as XT
// set-1 make/break codes; the native ISR body updates the game's own key-state bytes exactly as
// FUN_00027E78 does. Nothing reads port 60h or acknowledges the PIC.
#include "seams/seams.h"

#include <deque>

namespace game::seams {
namespace {

using hlekit::Machine;
using hlekit::Reg;
using hlekit::Seam;

std::deque<uint16_t> g_biosKeys;

uint8_t Ascii(uint8_t scan, bool shift) {
    static const char plain[] = "\0\x1b" "1234567890-=\b\tqwertyuiop[]\r\0asdfghjkl;'`\0\\zxcvbnm,./\0*\0 ";
    static const char shifted[] = "\0\x1b" "!@#$%^&*()_+\b\tQWERTYUIOP{}\r\0ASDFGHJKL:\"~\0|ZXCVBNM<>?\0*\0 ";
    if (scan < sizeof plain - 1) return (uint8_t)(shift ? shifted[scan] : plain[scan]);
    return 0;
}

// One raw keyboard byte as FUN_00027E78 handles it, minus `in 60h`, the controller status poll
// and the EOI. The game's tables are read from guest memory, so the mapping stays the game's.
void KeyByte(Machine& m, uint8_t raw) {
    m.vm.Wr8(D(m, kKeyRaw), raw);
    if (raw == 0xe0) { m.vm.Wr8(D(m, kKeyExtended), 1); return; }
    const uint8_t key = raw & 0x7f;
    const bool release = (raw & 0x80) != 0;
    const bool extended = m.vm.Rd8(D(m, kKeyExtended)) != 0;
    if (key == 0x2a && extended) {  // extended Shift is ignored by the original
        m.vm.Wr8(D(m, kKeyExtended), 0);
        return;
    }
    int index = -1;
    for (int i = 0; i < 29; ++i) {
        if (m.vm.Rd8(D(m, kKeyScancodes) + (uint32_t)i) == key) { index = i; break; }
    }
    auto setLast = [&] { m.vm.Wr8(D(m, kKeyLast), release ? 0 : key); };
    if (index < 0) {
        setLast();
        m.vm.Wr8(D(m, kKeyExtended), 0);
        return;
    }
    const uint16_t action = m.vm.Rd16(D(m, kKeyActions) + (uint32_t)index * 2);
    const uint8_t type = (uint8_t)action;
    const uint8_t bit = (uint8_t)(action >> 8);
    auto update = [&](uint32_t at) {
        const uint8_t was = m.vm.Rd8(D(m, at));
        m.vm.Wr8(D(m, at), release ? (uint8_t)(was & ~bit) : (uint8_t)(was | bit));
    };
    switch (type) {
    case 0: update(kKeyState0); break;
    case 1: update(kKeyState1); break;
    case 2: update(kKeyState2); setLast(); break;
    case 3: update(kKeyState3); setLast(); break;
    default: break;
    }
    m.vm.Wr8(D(m, kKeyExtended), 0);
}

// FUN_00027FB7: int 16h/01h then /00h. Native: the host key queue, no BIOS.
uint16_t GetBiosKey() {
    if (g_biosKeys.empty()) return 0;
    const uint16_t k = g_biosKeys.front();
    g_biosKeys.pop_front();
    return k;
}

}  // namespace

SeamList Input() {
    return {
        {0x27fb7, "bios_get_key", "input",
         "FUN_000027FB7: int 16h/01h (key waiting?) then int 16h/00h (read it); AX = 0 if none. "
         "Native: the host key queue; also the idle point of key-wait loops, paced to 70 Hz.",
         [](Machine& m) {
             PaceToTick(m);
             m.vm.Set16(Reg::Eax, GetBiosKey());
         }, 0, 0, Seam::Ret::Near32},
    };
}

void OnKey(hlekit::Machine& m, uint8_t xtScan, bool down, bool extended) {
    if (extended) KeyByte(m, 0xe0);
    const uint8_t raw = (uint8_t)(xtScan | (down ? 0 : 0x80));
    KeyByte(m, raw);
    // F9, or Ctrl+S: save where Twinsen stands
    if (down && (xtScan == 0x43 || (xtScan == 0x1f && (m.vm.Rd8(D(m, kKeyState1)) & 0x04)))) RequestQuickSave(m);
    if (down) {
        const bool shift = (m.vm.Rd8(D(m, kKeyState1)) & 0x20) != 0;
        uint16_t k = (uint16_t)(xtScan << 8) | Ascii(xtScan, shift);
        g_biosKeys.push_back(k);
        if (g_biosKeys.size() > 32) g_biosKeys.pop_front();
    }
}

void OnMouse(hlekit::Machine& m, int x, int y, uint8_t buttons) {
    (void)m;
    (void)x;
    (void)y;
    (void)buttons;
}

void OnPad(hlekit::Machine& m, const hlekit::PadState& pad) {
    (void)m;
    (void)pad;
}

}  // namespace game::seams
