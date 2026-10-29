# Licensing

Dinghy is free software under the GNU General Public License, version 3 only (`GPL-3.0-only`, full text in
`LICENSE`). You may use, study, change and share it. If you give someone a firmware built from it, give them its
complete source under the same licence too.

## Who made what

| | |
| --- | --- |
| The platform: `firmware/hal/`, `firmware/loader/`, `crt0.S`, `app.ld`, `firmware/src/` (`usb.c`, `ota.c`, `recovery.c`, `lcd.c`, `gfx.c`, `libc.c`, `bootguard.h`, `uac_fir.h`), `tools/` | Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments ([Felucca](https://github.com/hugelton/Felucca)), with the changes of [SLOOP](https://github.com/isod89/sloop-fm1) (isod89) |
| The SMK-37 board layer: `firmware/hal/smk_input.h`, `fm1_flash_map.h`, the SMK-37 parts of `fm1_cc.h`, `fm1_flash.h`, `fm1_lcd_hw.h`, `lcd.c`, `ota.c`, `bootguard.h`, `firmware/loader/`, `tools/fm1pkg_make.py`, `tools/fm1_install.py`, `tools/build.py`; `firmware/src/smk_wheel.h`; `tests/ota_test.c`, `ldr_test.c`, `install_test.py`, `smk_wheel_test.c`, `smk_pkg_test.py` | Copyright (C) 2026 M. Geisler (genkigenki/makkuro), from [genkigenki/sloop-fm1](https://github.com/genkigenki/sloop-fm1) branch `smk37` (the reference port of the [fm1-to-smk37](https://github.com/genkigenki/fm1-to-smk37) kit) |
| The TRS MIDI IN `firmware/src/midi_uart.c` | Felucca's, as sloopDX carries it, with the ring read by content from Felucca [Salt] 0.9-salt14 by ChanceTheMaker (found by keremimo) |
| The update loader's screen: `firmware/loader/ldr_screen.c`, `tests/ldr_screen_test.c` | Copyright (C) 2026 zvenson (from OberVave, the Firmware Hub's look); the idea, a loader that draws, its 5 x 7 letters and the order of things from [Skiff](https://github.com/illuminus5/skiff) by illuminus5, GPL-3.0 |
| From OberVave: the storage (blobs, slots), `LCD_IDLE`, the SMK-37 wiring of `platform.c`, `store.h`, the pad event queue in `smk_input.h`, `tests/smk_flash_test.c`, `tests/run_tests.sh`, the CI workflow | Copyright (C) 2026 zvenson |
| Dinghy: the wiring `platform.c`, the host mode `tests/`, the docs (the app `dinghy.c`, which MEL grew out of) | zvenson |
| The tapes' block ADPCM: its format and predictor table (`tools/gen_tapes.py` BLK_, `firmware/src/mel.c` BLK_FILT) | from genkigenki's [Erik37](https://github.com/genkigenki/erik37) `app/dsp/erk_codec.h`, Copyright (C) 2026 M. Geisler, MPL-2.0 (GPL-compatible) |
| MEL on the SMK-37: `firmware/src/mel_smk.h`, `SMK37.md` | Copyright (C) 2026 M. Geisler (genkigenki/makkuro), from his SMK-37 build of FMEL-1; the panel's layout AntonioAltea |
| MEL: `firmware/src/mel.c`, `mel_fx.c`, `mel_seq.c`, `mel_ui.c`, `tools/gen_tapes.py`, `tools/get_tapes.sh`, `tools/get_piano.sh`, `tools/gen_sprites.py`, `tools/gen_mel_fonts.py`, `web/emu/` (after the browser emulators of X0X, Felucca and FoMni, GPL-3.0) | AntonioAltea |

## Third-party material

| What | Licence | Where |
| --- | --- | --- |
| Terminus font 8x16 (ter-u16n), rendered into `firmware/gen/felucca_font.h` | SIL OFL 1.1 | `assets/fonts/Terminus-LICENSE.txt` |
| Terminus Bold 16, 20 and 24 px (Debian's console fonts), rendered into `firmware/gen/mel_fonts.h` by `tools/gen_mel_fonts.py` | SIL OFL 1.1 | `assets/fonts/Terminus-LICENSE.txt` |
| JieLi AC79 SDK: `uboot.boot`, `cfg_tool.bin`, `eq_cfg_hw.bin` are read from your SDK checkout at build time and placed in the package; no SDK files are in this tree | Apache-2.0 | <https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK> |
| MEL's tapes: Taijiguy's (Bernie Kornowicz) Mellotron M400S samples, packed by `tools/gen_tapes.py` into `firmware/gen/mel_tapes.h` and so into every build; the originals (`tools/get_tapes.sh`, into the ignored `tapes/`) are not in this tree | his, not GPL: "free to use, but cannot be used in any commercial, for profit software" ([KVR, 2021](https://www.kvraudio.com/forum/viewtopic.php?t=561309)) | <http://leisureland.us> (via the Wayback Machine) |
| MEL's piano: a Steinway B from Versilian Studios' VCSL (Sam Gossner), four keys as SLOOP cut them (its `assets/samples-cc0/PIANO`), packed by `tools/gen_tapes.py` into `firmware/gen/mel_tapes.h`; the originals (`tools/get_piano.sh`, into the ignored `tapes/`) are not in this tree | CC0 1.0 (public domain) | <https://github.com/sgossner/VCSL>, <https://github.com/isod89/sloop-fm1> |
| MEL's reverbs (`firmware/src/mel_fx.c`, REVERB's TYPE): HALL, ported from Felucca's HALL (`firmware/src/fx.c`: its lines, diffusers, Paley matrix, drift and gains); PLATE, Jon Dattorro's plate ("Effect Design, Part 1", JAES 1997) after FoMni's (`firmware/src/dsp/omni.c`), in integers at 22.05 kHz | GPL-3.0 | Leo Kuroshita (Hügelton Instruments), [Felucca](https://github.com/hugelton/Felucca); Charles Vestal, [FoMni](https://github.com/charlesvestal/fm1-fomni) |
| How the piano is made and played: SLOOP's `tools/gen_samples.py` ("piano"), `sampleio.py` and `firmware/src/eng_sample.c` with its LOFI KEYS preset, ported to `tools/gen_tapes.py` (`sloop_piano`) and `firmware/src/mel.c` (a kind 1 zone) | GPL-3.0 | Leo Kuroshita (Hügelton Instruments) and isod89, [SLOOP](https://github.com/isod89/sloop-fm1) |

## The drawings

The instruments on MEL's screen and its cassette, the snake (`assets/sprites/`, and `firmware/gen/mel_sprites.h`, made
from them by `tools/gen_sprites.py`) are drawn by Javi and used here with his permission. They are his: not under the
GPL unless he says so.

## The logo

`assets/logo.svg` / `logo.png` carry the designburg signet: Copyright (C) designburg, all rights reserved, not under
the GPL. It marks Dinghy itself; a fork uses its own logo.

## Trademarks

"Felucca" and "Hügelton Instruments" are names of Hügelton Instruments; "M-VAVE", "FM-1", "SMK-37" and "MKE-P37"
belong to their owners. Dinghy is not made or endorsed by them.
