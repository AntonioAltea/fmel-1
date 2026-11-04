# FMEL-1 on the M-Vave SMK-37

FMEL-1 (MEL) also builds for the M-Vave SMK-37, a 37-key controller with the same chip as the FM-1. It is the same
MEL: the same tapes, sounds, pages, effects and screen, with 37 velocity keys, 16 pads, 4 faders, two wheels and a
pedal jack on top. The FM-1 build is unchanged; everything for the SMK-37 is behind `BOARD_SMK37`
(`firmware/src/mel_smk.h`), on [Dinghy](https://github.com/zvenson/dinghy)'s SMK-37 board layer, which is
genkigenki's (his [fm1-to-smk37](https://github.com/genkigenki/fm1-to-smk37) kit). genkigenki also made the first
SMK-37 build of FMEL-1.

Try it in the browser: <https://antonioaltea.github.io/fmel-1/smk37.html>.

## Supported models

- **SMK-37 Pro**: the default build (identity `SMK-37 Pro_45N`, N the minor version, as the FM-1's `FM-1_95N`).
- **SMK-37 Elite**: `--model elite` (identity `SMK-37 Elite_45N`).
- **MKE-P37**: `--model mke` (identity `MKE-P37_45N`).
- **Starrykey 37 Play**: the default build.

All of them are the same hardware. Each build carries its model's name, so M-Vave's updater still takes that unit's
stock package later: install the package for your model.

## The tapes

The Mellotron M400S tapes are Bernie Kornowicz's (Taijiguy), as in the README: free to use, never in anything sold.
They are inside the SMK-37 packages too.

## Controls

Every control on the surface does one thing, always. A light means something is not as it was.

| | |
| --- | --- |
| Keys | all 37, C3..C6 at OCTAVE 0; how hard you play is how loud the tape: SETUP's VELOCITY is how much (0%: every key as loud, the flute's within 1 dB, 50%: the softest 20 dB down, 100%: 40 dB); the SEQ's steps play at full level |
| Pads, top row 1-6 | the SOUND: A FLUTE, B CHOIR, C CELLO, D VIBES, E SAX, F PIANO |
| Pads, bottom row 9-14 | the LAYER's sound, the same columns (the lit one again: no layer) |
| Pad 7 / 15, held | only the top row / only the bottom row heard |
| Pad 8 | HOLD: a tap switches it; held, it is on (or off) only while held |
| Pad 16 | HALF SPEED, the same way |
| Fader 1 | MIX: up the top row, down the bottom one |
| Fader 2 / 3 | how much LOFI / how much FX |
| Fader 4 | VOLUME |
| K1 / K2 / K3 / K4 | TONE / SELECT (the hand on the reel) / TEMPO / PITCH |
| K5..K8 | the page's four knobs (the plate on the screen) |
| < > | the section's pages, back and on |
| FADER BANK | UNDO: the last knob or pad change back; again: redone (lit while there is one) |
| Pitch wheel / mod wheel | the motor (+-2 semitones) / the vibrato's DEPTH, from the page's value (down) to 100% (up), and its MIX to fully wet |
| PEDAL jack | sustain (either kind of pedal) |
| PATCH, SCALE, NOTE REPEAT, FX, PARA, GLOBE, CHORD, ARP, SEQ, SEQ PLAY, SEQ REC, BT, OCTAVE- / + | the FM-1's HOME, SEL, LFO, FX, EDIT, GLO, ENV, ARP, SEQ, PLAY, REC, SAVE, OCT- / + |
| KNOB BANK held at power-on | the panel check |
| OCTAVE- held at power-on | the USB rescue |
| OCTAVE- and OCTAVE+ held 5 s | restart into the USB rescue |
| SEQ REC | recording on / off; held 1 s: the pattern cleared (again within 5 s: back) |

The pads light in each sound's colour, the chosen one brighter while it plays. Where a fader is, is its value: the
four are read, not saved. The SMK-37's TRS jack is a MIDI output; MIDI comes in over USB only.

## Build

```sh
./build.sh --board smk37                 # build/mel-smk37.fwsc        SMK-37 Pro, Starrykey 37 Play
./build.sh --board smk37 --model elite   # build/mel-smk37-elite.fwsc
./build.sh --board smk37 --model mke     # build/mel-smk37-mke.fwsc
./build.sh                               # the FM-1 last: tests/run_tests.sh reads its files
BOARD=smk37 tests/run_host.sh [script]   # on this computer: build/host/mel-smk37.wav and screens
```

## Install

```sh
python3 tools/fm1_install.py build/mel-smk37.fwsc
```

It checks that the package is for the connected keyboard, and over another custom firmware it stops and asks you to go
back to the stock firmware first (`--over-custom` installs anyway). Or use the web installer:
<https://antonioaltea.github.io/fmel-1/install/> (Chrome or Edge), choose SMK-37 and your model. It installs over
any firmware.

## Back to the stock firmware

Install M-Vave's package for your model with M-Vave's updater, M-UPGRADE. FMEL-1 never writes the keyboard's
calibration, Bluetooth pairing, key mapping or stock resources (Dinghy's `store.h` and the flash driver's guard).
