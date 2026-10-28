// Settings seam: the game's LBA.CFG reader. The original parses Language, LanguageCD,
// FlagKeepVoice and FlagDisplayText out of the text file and stores them in globals. Native: the
// same globals, filled from the port's config.ini (see PortOptions) instead, so the language is
// chosen in one place and never depends on the game folder's LBA.CFG, which stays read only.
#include "seams/seams.h"

namespace game::seams {
namespace {

using hlekit::Machine;
using hlekit::Seam;

// FUN_0001AAEC: Language -> index into the name table at 40378h (English, Francais, Deutsch,
// Espanol, Italiano); LanguageCD -> the same index plus a "voice on disc" flag; the two ON/OFF flags.
void ApplySettings(Machine& m) {
    auto& vm = m.vm;
    vm.Wr32(D(m, kLanguage), (uint32_t)m.OptionIndex("language"));
    const int voice = m.OptionIndex("voice_language");   // English, French, German, None
    uint32_t hasVoice = voice < 3 ? 1 : 0;
    if (voice < 3) vm.Wr32(D(m, kVoiceLanguage), (uint32_t)voice);
    if (vm.Rd32(D(m, kVoiceAvailable)) == 0) hasVoice = 0;   // the original's own gate
    vm.Wr32(D(m, kVoiceFlag), hasVoice);
    vm.Wr32(D(m, kKeepVoice), m.Option("keep_voice") == "ON");
    vm.Wr32(D(m, kDisplayText), m.Option("display_text") != "OFF");
}

}  // namespace

std::vector<hlekit::ConfigOption> PortOptions() {
    return {
        {"language", "English", "Text language of the game.", {"English", "French", "German", "Spanish", "Italian"}},
        {"voice_language", "English", "Spoken language (the .VOX files). None = no speech.", {"English", "French", "German", "None"}},
        {"keep_voice", "OFF", "Keep a voice playing until it finishes.", {"ON", "OFF"}},
        {"controls", "classic", "classic: Left/Right turn, Up walks forward (the original). direct: the arrows move Twinsen across the screen.", {"classic", "direct"}},
        {"widescreen", "off", "Widen the 3D scene to this aspect ratio. Menus and the HUD stay centered.", {"off", "16:9", "16:10", "21:9"}},
        {"display_text", "ON", "Show dialogue text on screen.", {"ON", "OFF"}},
    };
}

SeamList Settings() {
    return {
        {0x1aaec, "read_config", "settings",
         "FUN_0001AAEC: parses LBA.CFG (Language, LanguageCD, FlagKeepVoice, FlagDisplayText) into "
         "the globals at 41154h, 411F8h/411D0h, 411DCh, 411E0h. Native: the same globals from the "
         "port's config.ini.",
         ApplySettings, 0, 0, Seam::Ret::Near32},
    };
}

}  // namespace game::seams
