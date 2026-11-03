# Make it yours

Dinghy is meant to be copied. This is how a copy becomes your firmware, gets released, and gets into the
[FM-1 Firmware Hub](https://thefirmwarehub.com) next to the others.

## 1. Copy

On GitHub: **Use this template** (or fork). Then build once before changing anything, so you know your setup
works: `./build.sh` and `tests/run_host.sh` (see the [README](../README.md)).

## 2. Name it

- `dinghy.c` is yours: rename it if you like (and the one line in `tools/build.py` that compiles it).
- At its top, before `#include "platform.c"`:

  ```c
  #define APP_NAME "MYSYNTH"        /* the rescue and the crash screen say this */
  ```

- The package file names (`dinghy.fwsc`) are in `tools/build.py` and `.github/workflows/release.yml`: search for
  `dinghy`.
- The update loader's screen (FM-1) shows `SCREEN_NAME` in `SCREEN_COLOUR` (RGB565), both in `tools/build.py`.
- Your data: a `STORE_MAGIC` of your own in `firmware/src/store.h`, so your firmware never reads another Dinghy fork's
  settings as its own; for the SMK-37 also `DATA_FAMILY` in `tools/build.py` (the store's owner tag, 1..7 of A-Z 0-9).
- Make your own logo; `assets/logo.svg` is Dinghy's.

## 3. Your package identity

Every FM-1 package carries an identity like `FM-1_990`. The FM-1 reports the one it runs, and installers and the
hub use it to tell firmwares apart: two firmwares with the same identity look like one.

- Dinghy is `FM-1_99N` (N = the minor version; on the SMK-37 `SMK-37 Pro_99N`, `SMK-37 Elite_99N`, `MKE-P37_99N`).
  **Pick your own** in `tools/build.py` (`BOARD_PREFIX` and `PRODUCT`).
- Keep the form `FM-1_9` + two digits if you can: the web installers then also finish an install that was cut
  off halfway (they resume only the update loaders they know, `FM-1_9NN`).
- Taken so far: `900` SLOOP, `91N` Felucca, `93N` sloopDX, `97N` zp12, `98N` (a Doom port), `99N` Dinghy, and others
  in the hub's list. There is no registry yet: look at the hub, and ask there if you are unsure.

## 4. Release

```sh
./build.sh --release 0.2        # -> build/dinghy-0.2.fwsc, identity FM-1_992, the version "0.2" on the screen
```

On GitHub: a release with a tag (`v0.2`) and the `.fwsc` attached. Say what changed in its notes: the hub links
them.

Or let GitHub build it: push a tag `v0.2` and `.github/workflows/release.yml` builds the FM-1 and the three SMK-37
packages, runs `tests/run_tests.sh` and publishes the release with a build attestation (the proof that these files
were built from this source on GitHub; the hub can show it). It needs nothing from you but the tag.

## 5. Into the hub

The [FM-1 Firmware Hub](https://thefirmwarehub.com) installs open-source FM-1 firmwares with one click,
and checks every night for new releases. To be listed:

- the source is public and GPL-3.0 (Dinghy already is: keep it so);
- the releases are on GitHub, each with its `.fwsc`;
- a screenshot of your firmware's screen: `tests/run_host.sh` makes one (`build/host/*.png`), or a photo;
- you ask: the hub's *"Made an open-source firmware?"* card says how.

After that every release you make shows up there by itself.

## 6. Keep the credits

Your firmware stands on Felucca (Leo Kuroshita, Hügelton Instruments) and SLOOP (isod89), on the SMK-37 also on
genkigenki's board layer. The GPL asks you to
keep their copyright lines and to give your users your source; it also is just fair. Keep `LICENSING.md` and
the headers, and add yourself.

## Ideas to start from

- A second oscillator, detuned: two phases, one `inc` a little bigger.
- An envelope per key with the four knobs (`ENC_KNOB1..4`) as attack, decay, sustain, release.
- A one-pole low-pass: `y += (x - y) * k >> 15` per frame; `k` from a knob.
- A step sequencer on the white keys, `BTN_PLAY` to start, the tempo on `ENC_SELECT`.
- Look at how zp12 or sloopDX do it: same platform, much bigger apps.
