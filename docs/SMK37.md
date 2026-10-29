# Dinghy on the SMK-37

The M-Vave **SMK-37** (sold as SMK-37 Pro, SMK-37 Elite and MKE-P37: one keyboard, three names) runs on the FM-1's
chip, the JieLi AC791N. Dinghy builds for it from the same source: your app compiles unchanged, and `BOARD_SMK37` gives
you the keybed's velocity, the outer keys, the pads with their colours, the faders and the wheels
([API.md](API.md#the-smk-37) lists the calls).

The board layer (keybed, pads, LEDs, the flash map, the update path) is genkigenki's, from his
[sloop-fm1](https://github.com/genkigenki/sloop-fm1) port, the reference of the
[fm1-to-smk37](https://github.com/genkigenki/fm1-to-smk37) kit; it came into Dinghy as OberVave carries it, where it
runs on real SMK-37s.

## Build

```sh
./build.sh --board smk37                 # SMK-37 Pro:   build/dinghy-smk37.fwsc,       identity SMK-37 Pro_990
./build.sh --board smk37 --model elite   # SMK-37 Elite: build/dinghy-smk37-elite.fwsc, identity SMK-37 Elite_990
./build.sh --board smk37 --model mke     # MKE-P37:      build/dinghy-smk37-mke.fwsc,   identity MKE-P37_990
./build.sh                               # the FM-1 (build it last: the tests read its binaries)
```

The model name goes into the identity, the update loader and the update record: M-Vave's updater only takes a unit's
own factory package back, so each name needs its own package. `--release X.Y` works as on the FM-1.

On the host: `BOARD=smk37 tests/run_host.sh [script]`. A script line can then also hit a pad (`T pad P V`), move a
fader (`T fader F V`) or a wheel (`T wheel W RAW`), set the velocity of the keys (`T vel V`) or play an outer key
(`T xkey K V`); [tests/scripts/smk37.txt](../tests/scripts/smk37.txt) is an example.

## Install

The same installer as for the FM-1; it knows the SMK-37's names:

```sh
python3 tools/fm1_install.py build/dinghy-smk37.fwsc --yes
```

Back to the stock firmware: M-Vave's updater with the unit's factory package, or the
[Firmware Hub](https://thefirmwarehub.com/smk37/).

## What is different

| | FM-1 | SMK-37 |
| --- | --- | --- |
| keys | 27, no velocity | 37 with velocity; the middle 27 are `fm1_in.notes` |
| knobs | 7 encoders, MASTER pot | 8 encoders; K1 is the MASTER |
| LEDs | buttons and keys | buttons; the 16 pads in colour |
| MIDI IN | USB and TRS | USB (its UART pin is a key-matrix row) |
| store | settings 0xFC000, blobs 0xF4000, slots 0x97000 | settings 0x9D000, blobs 0x9E000, slots 0xC2000, owner tag 0xAF000 |
| update loader | draws its screen | dark |
| app slot | 0x8DFBC | 0x97400 |

**The store's owner.** The ports share the SMK-37's store under the kit's convention: a tag at 0xAF000
("SMK37DATA" + the family) says whose data it is. Dinghy's family is `DINGHY` (`DATA_FAMILY` in tools/build.py; give
your fork its own, 1..7 of A-Z 0-9). At boot, anything that is not yours (the stock settings, another port's data) is
cleared once and the tag written; it is never read.

**What is never touched.** The head (0x0..0x3FFF), BT pairing and the key / pad calibration (0xC0000..0xC1FFF), the
stock resources (0xF4000..0xFDFFF) and key_mac (0xFF000..). The app's allow-list keeps them out, and the RAM flash
driver refuses them on its own as well (`FL_GUARD`, hal/fm1_flash_map.h; tests/smk_flash_test.c checks every sector).
