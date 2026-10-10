# MEL

**A tape keyboard for the M-VAVE FM-1, after the Mellotron M400, with a love of old tape.** Every key has its own
tape: press it and the tape runs past the head; let go and a spring pulls it back. Flute, choir, cello and vibes,
from real M400 tapes (two at once if you like), through a tape machine that wows, flutters, hisses and wears, and a few pedals.
Built on [Dinghy](https://github.com/zvenson/dinghy) (its platform: updates, the USB rescue, USB-MIDI, USB audio,
the panel, the screen, settings).

**How it is laid out:** a button is a section; pressed again it turns its pages (SELECT turns them too). On every
page the four knobs do what the screen's aluminium plate says, each with its name and value; on FX pages the last
one is always MIX. PRESETS is the sound (A FLUTE, B CHOIR, C CELLO, D VIBES: a track selector like the M400's) and ALGORITHM the
tempo, on every page.

| Button | Section | Pages: KNOB 1 · 2 · 3 · 4 |
| --- | --- | --- |
| **HOME** | PLAY | TONE · PITCH (semitones) · ATTACK · RELEASE |
| **SEL** | LAYER | a second sound on the same tape's other track: SOUND (OFF, A..D) · MIX · OCTAVE · DETUNE. On its page SEL again turns the SOUND; on PLAY you see both, the louder one bigger. Lit: a layer sounds |
| **LFO** | LOFI | TAPE: WOW · FLUTTER · DRIVE (clean to a fat saturation, after the JHS 424 cassette preamp) · HISS — WEAR: AGE (a copy of a copy: the band narrows, the copying deck adds hiss, flutter, drive) · DROPOUT · DRIFT (every key its own tuning, up to +-25 cents) · CRUSH (bits, rate) |
| **FX** | FX | VIBRATO: RATE · DEPTH · TONE · MIX (half way: a chorus) — LESLIE: SPEED · DRIVE · SPREAD · MIX — ECHO: TIME · REPEATS · TONE · MIX — REVERB: SIZE · TONE · PRE · MIX |
| **EDIT** | EQ | LOW · MID · FREQ (the mid's) · HIGH |
| **GLO** | SETUP | TEMPO (for ARP and SEQ) · TUNE (fine, to play along with others) · LENGTH (as held / 8 s as on the M400) · SCREEN (old tape / clean); the screen says what each does |
| **ARP** | ARP | on / off and its page: MODE · RATE · OCTAVES · GATE |
| **SEQ** | SEQ | 16 steps of up to 4 notes: LENGTH · GATE · STEP · SWING. PLAY starts / stops, REC records (stopped: step by step, where SEL is a rest and SEL held clears; playing: into its step) |
| **LFO / FX held** | bypass | 0.6 s: that section out / in, each on its own (LOFI: a clean tape, steady motor, keys in tune; FX: the pedals, the echo and the reverb ringing out). The EQ stays. The button blinks while its section is out |
| **ENV** | HOLD | the notes stay after you let go; the next chord replaces them |

OCT- OCT+ is the octave, SAVE saves (it
also saves by itself, once nothing sounds). USB-MIDI: notes (any channel), sustain pedal, mod wheel = vibrato,
pitch bend = the motor (+-2 semitones). WOW and FLUTTER are the tape's motor (irregular, wandering); the vibrato
is a pedal (regular, musical): they are not the same thing.

The screen is an M400 played back from an old tape: the white body (the reel, the section and its page, the lamps
that are on), the stage (the instrument drawn and moving as it sounds, a cassette, a worn waveform, the vibrato's
wave, a Leslie turning, an echo's tape loop, the reverb dying away, the EQ's curve), the tape frame (one strip per
key, the oxide running past the head) and the brushed aluminium plate. Scanlines; the picture sways with the wow,
tears on a dropout, gets grainy with AGE (SETUP > LOOK: clean).

> **The tapes are not in this repository.** They are Taijiguy's (Bernie Kornowicz) Mellotron M400S samples:
> free to use, not in commercial software. `tools/get_tapes.sh` fetches them into `tapes/` (ignored by git), and
> the build packs them in: a MEL build is for you, not to give away.

## Try it in the browser

```sh
pip3 install Pillow numpy
tools/get_tapes.sh                          # the tapes, ~90 MB, into tapes/
python3 -m venv .venv-zig && .venv-zig/bin/pip install ziglang   # the WebAssembly compiler, once
web/emu/build.sh                            # -> build/emu/
python3 -m http.server -d build/emu 8790    # then http://localhost:8790/
```

The page is the FM-1 running the same `mel.c` (as Felucca's, FoMni's and X0X's web emulators do): mouse, the
computer keyboard (Z..Y rows) or a MIDI keyboard (Chrome / Edge).

## On the FM-1

You need the JieLi toolchain and three files of the JieLi AC79 SDK (once):

```sh
tools/get_toolchain.sh                      # JieLi's clang for the chip, to ~/.jieli/toolchain
git clone --depth 1 --branch AC79NN_SDK_V1.2.1_2023-12-13 \
    https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK.git ~/fw-AC79_AIoT_SDK
```

Then:

```sh
./build.sh                                  # -> build/mel.fwsc (identity FM-1_MEL)
tests/run_host.sh                           # -> build/host/mel.wav and screen PNGs, no FM-1 needed
python3 tools/fm1_install.py build/mel.fwsc # install over USB (pip3 install mido python-rtmidi)
```

Back to another firmware with the [FM-1 Firmware Hub](https://fm1.designburgapps.com), or hold **OCT-** while
switching on for the USB rescue. Installing firmware is at your own risk.

**The tapes in the flash:** 4 sets x 12 roots (one every 3 semitones), IMA ADPCM at 11025 Hz (the tapes hold
almost nothing above 5 kHz), ~1.8 s each with a crossfaded loop (the vibes ~1.1 s: their decay is measured, taken
out of the loop and put back as the tape runs), every set as loud as the others: 439 KB.
That is about all the app slot has room for (an ARM build of MEL puts it near 550 of 581 KB): `--budget` lower if
`./build.sh` says the image is too big.
`--sets` picks others (MkII_Violins is there too). `tools/gen_tapes.py --budget`,
`--step`, `--rate` trade length, roots and bandwidth.

## The platform

What Dinghy gives MEL:

| | |
| --- | --- |
| **Updates that always work** | `tools/fm1_install.py`, the web installers and the [FM-1 Firmware Hub](https://fm1.designburgapps.com) reach your firmware over USB, so you (and your users) can always install the next build, or go back to the official firmware. |
| **USB rescue** | Hold **OCT-** while switching on: no sound, only the updater. If your firmware crashes twice at boot, the FM-1 goes there by itself. |
| **USB-MIDI** | In and out, class compliant: `midi_get()`, `midi_send()`. |
| **USB audio** | The FM-1 shows up as a 16-bit stereo audio input (44.1 or 48 kHz): record it into any DAW or phone. Nothing to do. |
| **Panel** | 27 keys, 14 buttons, 7 encoders, the MASTER knob, the LEDs, by name (`BTN_PLAY`, `ENC_KNOB1`, ...). |
| **Screen** | 240 x 240, text in two sizes, rectangles, lines, pixels. |
| **Settings** | `settings_load()` / `settings_save()`: up to 256 bytes with a CRC in their own flash sector. |
| **A crash screen** | A fault shows where it happened (the PC), then the FM-1 restarts. |
| **Host mode** | The same `mel.c` on your computer: the screen as an image, the sound as a WAV. No FM-1 needed to try an idea. |
| **Checks** | The build fails on what bricks or breaks an FM-1 firmware: a 64-bit division, a call into the chip ROM, too much RAM, a register touched outside `hal/`. |

### Where to go next

- **[docs/API.md](docs/API.md)**: everything `platform.c` gives you, with the panel's ids, the screen calls, the
  audio contract, MIDI, settings, the flash map, timing.
- **[docs/MAKE-IT-YOURS.md](docs/MAKE-IT-YOURS.md)**: turn Dinghy into your firmware: the name, the package
  identity, releases, getting into the FM-1 Firmware Hub.
- **[docs/RULES.md](docs/RULES.md)**: the chip's rules (no FPU, no libgcc, the audio ISR's budget) and what the build
  checks for you. Read it before your first crash, not after.

## License

GPL-3.0-only, as Felucca and SLOOP: you may change and share it, and whoever gets your firmware gets its source.
Details, credits and third-party parts in [LICENSING.md](LICENSING.md).

The platform is Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments, with SLOOP's changes (isod89).
Dinghy (the app, the host mode, the docs) by zvenson. MEL (mel.c, the tapes' tools, the web emulator) on top of it.
The tapes: Taijiguy (Bernie Kornowicz), not under the GPL and not in this tree.
