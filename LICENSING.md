# Licensing

Dinghy is free software under the GNU General Public License, version 3 only (`GPL-3.0-only`, full text in
`LICENSE`). You may use, study, change and share it. If you give someone a firmware built from it, give them its
complete source under the same licence too.

## Who made what

| | |
| --- | --- |
| The platform: `firmware/hal/`, `firmware/loader/`, `crt0.S`, `app.ld`, `firmware/src/` (`usb.c`, `ota.c`, `recovery.c`, `lcd.c`, `gfx.c`, `libc.c`, `bootguard.h`, `uac_fir.h`), `tools/` | Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments ([Felucca](https://github.com/hugelton/Felucca)), with the changes of [SLOOP](https://github.com/isod89/sloop-fm1) (isod89) |
| Dinghy: the wiring `platform.c`, the host mode `tests/`, the docs (the app `dinghy.c`, which MEL grew out of) | zvenson |
| MEL: `firmware/src/mel.c`, `mel_fx.c`, `mel_seq.c`, `mel_ui.c`, `tools/gen_tapes.py`, `tools/get_tapes.sh`, `tools/gen_sprites.py`, `tools/gen_mel_fonts.py`, `web/emu/` (after the browser emulators of X0X, Felucca and FoMni, GPL-3.0) | AntonioAltea |

## Third-party material

| What | Licence | Where |
| --- | --- | --- |
| Terminus font 8x16 (ter-u16n), rendered into `firmware/gen/felucca_font.h` | SIL OFL 1.1 | `assets/fonts/Terminus-LICENSE.txt` |
| Terminus Bold 16, 20 and 24 px (Debian's console fonts), rendered into `firmware/gen/mel_fonts.h` by `tools/gen_mel_fonts.py` | SIL OFL 1.1 | `assets/fonts/Terminus-LICENSE.txt` |
| JieLi AC79 SDK: `uboot.boot`, `cfg_tool.bin`, `eq_cfg_hw.bin` are read from your SDK checkout at build time and placed in the package; no SDK files are in this tree | Apache-2.0 | <https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK> |
| MEL's tapes: Taijiguy's (Bernie Kornowicz) Mellotron M400S samples, packed by `tools/gen_tapes.py` into `firmware/gen/mel_tapes.h` and so into every build; the originals (`tools/get_tapes.sh`, into the ignored `tapes/`) are not in this tree | his, not GPL: "free to use, but cannot be used in any commercial, for profit software" ([KVR, 2021](https://www.kvraudio.com/forum/viewtopic.php?t=561309)) | <http://leisureland.us> (via the Wayback Machine) |

## The drawings

The instruments on MEL's screen (`assets/sprites/`, and `firmware/gen/mel_sprites.h`, made from them by
`tools/gen_sprites.py`) are drawn by Javi and used here with his permission. They are his: not under the GPL unless he
says so.

## The logo

`assets/logo.svg` / `logo.png` carry the designburg signet: Copyright (C) designburg, all rights reserved, not under
the GPL. It marks Dinghy itself; a fork uses its own logo.

## Trademarks

"Felucca" and "Hügelton Instruments" are names of Hügelton Instruments; "M-VAVE" and "FM-1" belong to their
owners. Dinghy is not made or endorsed by them.
