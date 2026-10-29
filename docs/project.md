# Little Big Adventure — function-level HLE

The original `lba.exe` runs on dos-hle-kit; only the functions at the hardware seams are replaced
by native code. Use any decompiler you like for names and function boundaries; the kit does not
require a particular workspace format.

The seam table is split by area under `src/seams/` (video, input, sound, timing). `src/game.cpp`
only wires the modules together.

| | |
| :-- | :-- |
| Game directory | Not compiled in: found at startup by `--game DIR` (or an EXE path), `$HLEKIT_GAME_DIR`, the remembered directory, or next to the program (never written: writes go to the overlay) |
| Config file | `config.ini` in the per-user directory (SDL pref path, `~/.local/share/hlekit/little-big-adventure/` on Linux), generated on first run: `language`, `voice_language`, `keep_voice`, `display_text`. Override one run with `--set language=German`. Applied by the `read_config` seam in `src/seams/settings.cpp`; the game folder's `LBA.CFG` is no longer read |
| Sound | Music: `src/seams/music.cpp` (the game\'s play/stop/status/fade/volume functions onto the kit\'s XMIDI sequencer and FluidSynth; set `soundfont` in config.ini). Samples: `src/seams/samples.cpp` (the game\'s 18-entry sample API stubs at 28BD7h onto a native software mixer and the host audio sink; entry semantics read from `w_sb16.dll`). CD audio: `sound.cpp` (no disc). The driver DLLs are never loaded: `LBA.CFG` is generated with native device names (`src/game.cpp`) so the original init flow runs and reaches the seamed leaf functions |
| Widescreen | `widescreen = off / 16:9 / 16:10 / 21:9` in config.ini; `src/seams/widescreen.cpp`. The game stays 640x480 and its UI stays centered; the room is drawn again shifted to fill the sides (bricks by moving the brick origin at 17245h, actors by running AffScene\'s per-frame path with XCenter moved), between a state snapshot and restore. Full-screen pictures are pillarboxed. The room loop is paced to 70 Hz |
| Controls | `controls = classic / direct` in config.ini; `src/seams/controls.cpp`. classic is the original tank scheme. direct: the arrows name a screen direction, the seam at 108C7h (the main loop\'s key snapshot) steps Twinsen\'s stored heading toward it a little each frame and, once he faces it, presents a plain Up so the game\'s own walk logic runs. Not yet checked on ladders, swimming or in fights |
| Save anywhere | `save_anywhere = on / off` in config.ini; `src/seams/savegame.cpp`. The original autosaves only when a room is entered (FUN_0001EDAC writes `sNNNN.LBA`, recording the room\'s entry point). F9 or Ctrl+S sets the saved-position words (13C29Ah x, 13C296h y, 13C292h z) to Twinsen\'s position for one call of that writer, so loading puts him back where he stood. Only while he is under the player\'s control; a message is drawn on the host picture. Loading a save replays the short intro sequence (Escape skips it) |
| Skip intro | `skip_intro = on / off` in config.ini; `src/seams/intro.cpp`. An instruction seam at 11E15h in `main` (FUN_00011988) resumes at 11E7Eh, past the logo screens (FUN_000212E0 + FUN_00021348 waits) and the DRAGON3 FLA (FUN_00025C50), at the load of the menu background. When off, the handler runs the skipped calls itself |
| Function labels | `docs/functions.csv` (optional CSV: address,name,size) |
| Seam candidates | [seam-candidates.md](seam-candidates.md) (generated from the executable) |
| Ranked candidates | [ranked-seams.md](ranked-seams.md) (heuristic video/sound/timing/input ranking) |
| Seam modules | `src/seams/` (by area) |
| Decompiled code | `src/decompiled/` (grows over time) |

## Layout

```text
src/
  game.cpp        GameInfo and module wiring only
  seams/          seam modules by area + All(), which concatenates them
  decompiled/     translated game code, by subsystem (grows over time)
```

The seam table is a plain `std::vector<Seam>`: add new source files to `meson.build` and
concatenate their vectors in `seams::All()`. Start with HLE seams, split them by area, then
translate game code under `src/decompiled/`, calling the original through
`Machine::CallGuest`/`CallGuestFar` while a module is incomplete. A full decomp keeps the seam
boundary and the original executable as the reference/fallback.

## Run

```bash
meson setup build && ninja -C build
build/lba-hle                                   # window + sound
build/lba-hle --seams                           # the seam table, all modules
build/lba-hle --headless --overlay /tmp/ov --seconds 10 --png shot.png   # scripted, hermetic
```
