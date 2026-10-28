# Ranked seam candidates: lba.exe

Format: **LE** (32-bit). Functions: 898 from functions CSV `/tmp/lba-functions.csv`.

Evidence is combined with identification confidence and call-site hotness into a
**replacement value**. Five high-value seams can beat fifty arbitrary ones; the shortlist
below is cross-category, not just the top of one hardware table.

**The target is function replacement, not device emulation.** A port hit only locates the
seam. Implement its intent natively (host audio, frames/palette, host clock, host input);
do not build a Sound Blaster/AdLib/VGA/PIT/keyboard-controller model.


## Replacement shortlist (best value across areas)

| # | priority | confidence | function | addr | area | value | why |
| --: | :-- | --: | :-- | :-- | :-- | --: | :-- |
| 1 | Very high | 98% | FUN_00027e78 | `00027e78` | input | 82.4 | direct evidence; installed handler |
| 2 | Very high | 75% | FUN_00027fd4 | `00027fd4` | video | 27.5 | direct evidence; 12 direct callers |
| 3 | Very high | 75% | FUN_0002701a | `0002701a` | video | 26.2 | direct evidence; 26 direct callers |
| 4 | Very high | 75% | FUN_00028028 | `00028028` | video | 18.7 | direct evidence; 2 direct callers |
| 5 | Very high | 88% | FUN_0002f314 | `0002f314` | video | 18.6 | direct evidence |
| 6 | Very high | 88% | FUN_0002f323 | `0002f323` | video | 18.6 | direct evidence |
| 7 | Very high | 73% | FUN_0002611d | `0002611d` | video | 15.7 | direct evidence; 1 direct caller |
| 8 | Medium | 75% | FUN_00027fb7 | `00027fb7` | input | 14.2 | direct evidence; 1 direct caller |
| 9 | Medium | 40% | FUN_0002fe78 | `0002fe78` | timing | 9.5 | propagated from callers; 3 direct callers |
| 10 | Medium | 40% | FUN_0002f4e0 | `0002f4e0` | timing | 7.6 | propagated from callers; 1 direct caller |
| 11 | Medium | 40% | FUN_0002f568 | `0002f568` | timing | 7.6 | propagated from callers; 1 direct caller |
| 12 | Medium | 40% | FUN_0002fc28 | `0002fc28` | timing | 7.6 | propagated from callers; 1 direct caller |

## video (28 candidates, showing 25)

| rank | priority | confidence | function | addr | size | value | evidence |
| --: | :-- | --: | :-- | :-- | --: | --: | :-- |
| 1 | Very high | 75% | FUN_00027fd4 | `00027fd4` | 84 | 27.5 | port 3C8h DAC write idx x4 (VGA range) |
| 2 | Very high | 75% | FUN_0002701a | `0002701a` | 691 | 26.2 | VGA aperture access x4 |
| 3 | Very high | 75% | FUN_00028028 | `00028028` | 83 | 18.7 | port 3C8h DAC write idx x4 (VGA range) |
| 4 | Very high | 88% | FUN_0002f314 | `0002f314` | 15 | 18.6 | address stored in data; int 10h x1 |
| 5 | Very high | 88% | FUN_0002f323 | `0002f323` | 175 | 18.6 | address stored in data; int 10h x1 |
| 6 | Very high | 73% | FUN_0002611d | `0002611d` | 17 | 15.7 | calls FUN_00026112; int 10h x1 |
| 7 | High | 73% | FUN_00026112 | `00026112` | 11 | 14.7 | int 10h x1 |
| 8 | High | 73% | FUN_00026090 | `00026090` | 89 | 12.7 | int 10h x1 |
| 9 | High | 68% | FUN_0001eb48 | `0001eb48` | 100 | 10.0 | VGA aperture access x1 |
| 10 | Low (helper) | 70% | FUN_00026e7c | `00026e7c` | 19 | 7.9 | port 3DAh VGA status x2 (VGA range) |
| 11 | Medium | 40% | FUN_00025230 | `00025230` | 1079 | 7.6 | calls FUN_00026e7c; calls FUN_0002701a; calls FUN_00028028; calls leaf helper FUN_00026e7c; leaf helper caller |
| 12 | Low | 39% | FUN_0001ceac | `0001ceac` | 191 | 6.6 | calls FUN_00026e7c; calls FUN_00027fd4; calls leaf helper FUN_00026e7c; leaf helper caller |
| 13 | Low (helper) | 75% | FUN_0002807b | `0002807b` | 46 | 6.5 | port 3C8h DAC write idx x4 (VGA range) |
| 14 | Low | 40% | FUN_0001a9c0 | `0001a9c0` | 297 | 6.5 | calls FUN_0002701a; calls FUN_0002807b; calls leaf helper FUN_0002807b; leaf helper caller |
| 15 | Low | 36% | FUN_0001ce80 | `0001ce80` | 42 | 6.2 | calls FUN_0002807b; calls leaf helper FUN_0002807b; leaf helper caller |
| 16 | Medium | 73% | FUN_0002612e | `0002612e` | 11 | 6.1 | int 10h x1 |
| 17 | Low | 38% | FUN_00010864 | `00010864` | 3589 | 5.8 | calls FUN_00026e7c; calls FUN_0002701a; calls leaf helper FUN_00026e7c; leaf helper caller |
| 18 | Low | 39% | FUN_0001cdc4 | `0001cdc4` | 74 | 5.8 | calls FUN_00026e7c; calls FUN_00027fd4; calls leaf helper FUN_00026e7c; leaf helper caller |
| 19 | Low | 34% | FUN_0001cd80 | `0001cd80` | 67 | 5.5 | calls FUN_00026e7c; calls leaf helper FUN_00026e7c; leaf helper caller |
| 20 | Low | 34% | FUN_0001ce48 | `0001ce48` | 55 | 5.1 | calls FUN_00026e7c; calls leaf helper FUN_00026e7c; leaf helper caller |
| 21 | Low | 34% | FUN_0001cf6c | `0001cf6c` | 51 | 3.7 | calls FUN_00026e7c; calls leaf helper FUN_00026e7c; leaf helper caller |
| 22 | Low | 34% | FUN_0001cfa0 | `0001cfa0` | 49 | 3.7 | calls FUN_00026e7c; calls leaf helper FUN_00026e7c; leaf helper caller |
| 23 | Low | 34% | FUN_0001ea58 | `0001ea58` | 69 | 3.7 | calls FUN_00026112; calls FUN_00027fd4 |
| 24 | Low | 34% | FUN_0001ce10 | `0001ce10` | 53 | 3.2 | calls FUN_00026e7c; calls leaf helper FUN_00026e7c; leaf helper caller |
| 25 | Low | 34% | FUN_0001e170 | `0001e170` | 882 | 3.2 | calls FUN_00026e7c; calls leaf helper FUN_00026e7c; leaf helper caller |

## sound (0 candidates, showing 0)

| rank | priority | confidence | function | addr | size | value | evidence |
| --: | :-- | --: | :-- | :-- | --: | --: | :-- |

## timing (6 candidates, showing 6)

| rank | priority | confidence | function | addr | size | value | evidence |
| --: | :-- | --: | :-- | :-- | --: | --: | :-- |
| 1 | Low (helper) | 80% | FUN_000283b4 | `000283b4` | 22 | 10.9 | cli/sti; port 40h PIT ch0 x2 (PIT); port 43h PIT cmd x1 (PIT) |
| 2 | Low (helper) | 80% | FUN_0002fbf8 | `0002fbf8` | 48 | 10.9 | cli/sti; port 40h PIT ch0 x2 (PIT); port 43h PIT cmd x1 (PIT) |
| 3 | Medium | 40% | FUN_0002fe78 | `0002fe78` | 79 | 9.5 | calls FUN_0002fbf8; calls leaf helper FUN_0002fbf8; leaf helper caller |
| 4 | Medium | 40% | FUN_0002f4e0 | `0002f4e0` | 134 | 7.6 | calls FUN_000283b4; calls leaf helper FUN_000283b4; leaf helper caller |
| 5 | Medium | 40% | FUN_0002f568 | `0002f568` | 73 | 7.6 | calls FUN_000283b4; calls leaf helper FUN_000283b4; leaf helper caller |
| 6 | Medium | 40% | FUN_0002fc28 | `0002fc28` | 51 | 7.6 | calls FUN_0002fbf8; calls leaf helper FUN_0002fbf8; leaf helper caller |

## input (2 candidates, showing 2)

| rank | priority | confidence | function | addr | size | value | evidence |
| --: | :-- | --: | :-- | :-- | --: | --: | :-- |
| 1 | Very high | 98% | FUN_00027e78 | `00027e78` | 319 | 82.4 | address stored in data; installed handler (stored + cli/ports); port 60h kbd data x1 (keyboard data); port 64h kbd status x1 (keyboard controller) |
| 2 | Medium | 75% | FUN_00027fb7 | `00027fb7` | 15 | 14.2 | int 16h x2 |
