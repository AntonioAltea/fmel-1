# FMEL-1 on the M-Vave SMK-37

FMEL-1 (MEL) also builds for the M-Vave SMK-37, a 37-key controller with the same chip as the FM-1. The FM-1 build is
unchanged; everything for the SMK-37 is behind `BOARD_SMK37`. On the SMK-37 it is the same MEL: the same tapes,
sounds, pages, effects and screen, with 37 velocity keys, 16 pads, 4 faders and two wheels on top.

## Supported models

- **SMK-37 Pro**: the default build (package identity `SMK-37 Pro_45N`, N the minor version, as the FM-1's `FM-1_95N`).
- **SMK-37 Elite**: `MODEL=elite` (identity `SMK-37 Elite_45N`).
- **MKE-P37**: `MODEL=mke` (identity `MKE-P37_45N`).
- **Starrykey 37 Play**: uses the default build.

All of them are the same hardware. Each build carries its model's name, so M-Vave's updater still takes that
unit's stock package later: install the package for your model.

## The tapes

MEL plays the Mellotron M400S samples Bernie Kornowicz (Taijiguy) recorded from his 1973 M400S, serial #500, and
shared on his site [leisureland.us](http://leisureland.us). They are his work, not under this repository's GPL
licence, and they are inside every build, the SMK-37 packages too. His terms: "they are free to use, but cannot be
used in any commercial, for profit software." So share the SMK-37 packages freely, but don't sell them, nor anything
with these tapes in it. Thank you, Bernie.

## Controls

The FM-1's controls keep their MEL meaning, so the README's panel applies; the screen keeps the FM-1's names (SEL,
LFO, ALGORITHM, ...). The SMK-37 adds the rest.

| SMK-37 | MEL |
| --- | --- |
| Keys F3..G5 (the middle 27) | the FM-1's 27 keys, with velocity |
| Keys C3..E3 and G#5..C6 (the outer 10) | the notes below and above them: all 37 keys play, C3..C6 at OCTAVE 0. The M400 has 35 keys, G2..F5: with PITCH at -5 st (PLAY) the keyboard is G2..G5, the M400's 35 notes and two more, F#5 and G5, past its top (MEL pitches every note from its nearest tape) |
| Velocity | the tape's level, as if the head met the tape harder: a soft key about a third, a hard one full. USB-MIDI in velocity too; ARP and SEQ play at the velocity of the last note struck |
| Pads 1-4 (top row) | the SOUND: A FLUTE, B CHOIR, C CELLO, D VIBES (as PRESETS) |
| Pads 5-8 (top row) | the LAYER's sound A..D; the lit one again: no layer |
| Pad 9 | LOFI in / out (as LFO held) |
| Pads 10, 11, 13, 14 | VIBRATO, LESLIE, ECHO, REVERB on / off: the effect's MIX, remembered while it is off |
| Pad 12 | LESLIE FAST / SLOW |
| Pad 15 | HALF SPEED (as ENV held) |
| Pad 16 | LENGTH 8 SEC / HELD (SETUP) |
| Pad lights | off dark orange, on light blue, active bright: the sound, the layer and the effects that are on light up bright while notes sound |
| K1..K4 | KNOB 1..4 (the page's knobs, the plate's four) |
| K5 | MASTER (volume) |
| K6 / K7 / K8 | SELECT (the section's pages) / ALGORITHM (the tempo) / PRESETS (the sound) |
| PATCH | HOME: PLAY; again, the next sound |
| SCALE | SEL: LAYER |
| NOTE REPEAT | LFO: LOFI (held: LOFI out / in) |
| FX | FX (held: FX out / in) |
| PARA | EDIT: EQ (held: flat) |
| GLOBE | GLO: SETUP |
| CHORD | ENV: HOLD (held: HALF SPEED) |
| ARP / SEQ / SEQ PLAY / SEQ REC | ARP / SEQ / PLAY / REC |
| BT | SAVE |
| OCTAVE- / OCTAVE+ | OCT- / OCT+ |
| < / > | the previous / next page, all of them in a row (PLAY .. SEQ) |
| FADER BANK | what the faders set: A the tape (MIX, TONE, WOW, FLUTTER), B wear and space (AGE, DRIVE, ECHO MIX, REVERB MIX). Lit on bank B |
| Faders 1..4 | the bank's four values; a fader takes over only once it moves (also after a bank change). The header says the value; the plate lights the knob when its page is up |
| Pitch wheel | the motor: the tapes faster or slower, +-2 semitones (as a USB-MIDI pitch bend) |
| Mod wheel | the vibrato (as CC 1) |
| KNOB BANK held | the panel check: the flash part, the calibration, the ADC, faders, wheels, buttons, keys and pads |
| OCTAVE- held at power-on | the USB rescue (as OCT- on the FM-1) |

The keyboard on the screen shows all 37 keys. The SMK-37 has no key lights; the pads light instead. Its TRS jack is a
MIDI output and this build does not use it; there is no TRS MIDI IN, so MIDI comes in over USB only.

## Build

See the README for the toolchain.

```sh
BOARD=smk37 ./build.sh                 # build/mel-smk37.fwsc (SMK-37 Pro, Starrykey 37 Play)
BOARD=smk37 MODEL=elite ./build.sh     # build/mel-smk37-elite.fwsc
BOARD=smk37 MODEL=mke ./build.sh       # build/mel-smk37-mke.fwsc
./build.sh                             # the FM-1 build again before tests/run_tests.sh (the tests use its files)
sh tests/run_tests.sh                  # the host tests of both boards
BOARD=smk37 tests/run_host.sh          # MEL for the SMK-37 on this computer: build/host/mel-smk37.wav and screens
```

## Install

Connect the keyboard by USB and run, with the package for your model:

```sh
python3 tools/fm1_install.py build/mel-smk37.fwsc
```

`fm1_install.py` needs `mido` and `python-rtmidi` (`pip3 install mido python-rtmidi`). It checks that the package is
for the connected keyboard. Over another custom firmware it stops and asks you to go back to the stock firmware first
(`--over-custom` installs anyway). The SMK-37 web installer at <https://genkigenki.github.io/smk37/> takes the
packages too.

## Going back to the stock firmware

Install M-Vave's firmware package for your model with M-Vave's updater, M-UPGRADE. It works directly, because the
installed package keeps the keyboard's model name.

## Storage and safety

| Flash | MEL on the SMK-37 |
| --- | --- |
| `0x04000-0x9CFFF` | the app, with the tapes in it (529 KiB of the 605 KiB app slot; the tapes 0x144B0-0x7C2AF); only the update loader writes it |
| `0x9D000` | the settings (the FM-1 keeps them at `0xFC000`, the SMK-37's protected stock area) |
| `0xAF000` | the owner tag: at the first start, the stock firmware's settings there and any other firmware's data in these two sectors are cleared once |
| `0xBB000-0xBFFFF` | the update's staging area |
| `0xC2000-0xEAFFF` | USRFLASH, the stock key and knob mapping: never written, so the stock firmware finds its mapping intact |

- Back up first: have M-Vave's stock package for your model and M-UPGRADE ready before you install.
- Keep the USB cable connected until the install has finished and the keyboard has restarted.
- The firmware never writes the keyboard's head, calibration, Bluetooth pairing, USRFLASH, stock resources or key
  areas; the flash map refuses them at compile time and again in the flash driver.
- Installing other firmware is at your own risk.
