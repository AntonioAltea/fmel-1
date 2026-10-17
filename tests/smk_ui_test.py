#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# SMK-37 port: Copyright (C) 2026 M. Geisler (genkigenki/makkuro)
"""The SMK-37 mapping on the host (tests/host.c with BOARD_SMK37): the keys with velocity, the outer keys, the pads and
their lights, the fader banks, the wheels, the knobs and the pages. Builds build/host/mel-smk37-test, runs
tests/smk/*.txt, checks what comes out (MIDI out, the dumped state, the pad colours, the sound). Exit 1 on a failure.

  python3 tests/smk_ui_test.py
"""
import re
import struct
import subprocess
import sys
import wave
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build" / "host"
BIN = OUT / "mel-smk37-test"
OFF, ON, ACT = (110, 38, 0), (40, 120, 210), (190, 230, 255)   # the pads: off dark orange, on light blue, active bright
fails = 0


def check(name, ok, detail=""):
    global fails
    print(("  ok    " if ok else "  FAIL  ") + name + ("" if ok else f"  ({detail})"))
    fails += not ok


def run(script):
    out = subprocess.run([str(BIN), str(OUT / f"smk-{script}"), str(ROOT / "tests" / "smk" / f"{script}.txt")],
                         capture_output=True, text=True, check=True).stdout
    dumps = {}
    for ln in out.splitlines():
        m = re.match(r"\s*(\d+) ms  (PADS|STATE)(.*)$", ln)
        if m:
            dumps.setdefault(int(m[1]), {})[m[2]] = m[3].strip()
    return out, dumps


def state(d, t):
    return {k: int(v) for k, v in (kv.split("=") for kv in d[t]["STATE"].split())}


def pads(d, t):
    return [((c >> 8) & 255, (c >> 16) & 255, c & 255) for c in (int(x, 16) for x in d[t]["PADS"].split())]


def rms(path, t0, t1):
    with wave.open(str(path)) as w:
        fr = w.getframerate()
        w.setpos(int(t0 * fr / 1000))
        raw = w.readframes(int((t1 - t0) * fr / 1000))
    v = struct.unpack("<%dh" % (len(raw) // 2), raw)
    return (sum(x * x for x in v) / max(len(v), 1)) ** 0.5


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    subprocess.run(["cc", "-O1", "-Wall", "-Wno-unused-function", "-DDINGHY_HOST", "-DBOARD_SMK37=1", '-DAPP_VER="test"',
                    "-I" + str(ROOT / "firmware/src"), "-I" + str(ROOT / "firmware/gen"), "-o", str(BIN),
                    str(ROOT / "firmware/src/mel.c")], check=True)

    print("== keys: velocity, the outer keys")
    out, d = run("keys")
    check("a key sends its velocity (C4 at 10, then at 127)", "MIDI out 90 60 10" in out and "MIDI out 90 60 127" in out)
    soft, hard = rms(OUT / "smk-keys.wav", 600, 1100), rms(OUT / "smk-keys.wav", 1900, 2400)
    check("velocity sets the tape's level: a hard key is louder (> x1.8)", soft > 0 and hard / soft > 1.8, f"{soft:.0f} {hard:.0f}")
    check("the lowest key is C3 (keybed 0, OCTAVE 0), with its velocity", "MIDI out 90 48 90" in out and "MIDI out 80 48 0" in out)
    check("the highest key is C6 (keybed 36)", "MIDI out 90 84 70" in out)
    check("OCT- moves the outer keys too (C2)", "MIDI out 90 36 50" in out)
    check("HOLD: an outer key latches and lets go when the next chord comes",
          "MIDI out 90 50 100" in out and "MIDI out 90 81 100" in out)

    print("== pads: the sounds, the layer, the switches; their lights")
    out, d = run("pads")
    p = pads(d, 100)
    check("at rest: SOUND A on (light blue), B-D off (dark orange), no layer", p[0] == ON and p[1:8] == [OFF] * 7, p[:8])
    check("at rest: LOFI in, REVERB on (its MIX 18), the other switches off",
          p[8] == ON and p[13] == ON and all(p[i] == OFF for i in (9, 10, 11, 12, 14, 15)), p[8:])
    check("pad 2: SOUND B", state(d, 400)["sound"] == 1 and pads(d, 400)[1] == ON and pads(d, 400)[0] == OFF)
    check("pad 5: the LAYER is A", state(d, 700)["layer"] == 1 and pads(d, 700)[4] == ON)
    check("pad 5 again: the layer off", state(d, 1000)["layer"] == 0 and pads(d, 1000)[4] == OFF)
    check("pad 7: the LAYER is C", state(d, 1200)["layer"] == 3 and pads(d, 1200)[6] == ON)
    check("pad 9 twice within 20 ms: LOFI out once (a press counts once)", state(d, 1400)["lofi"] == 1 and pads(d, 1400)[8] == OFF)
    s = state(d, 2200)
    check("VIBRATO, LESLIE, ECHO on with a MIX; REVERB off", (s["vmix"], s["lmix"], s["emix"], s["rmix"]) == (100, 100, 30, 0), s)
    check("LESLIE FAST, HALF SPEED, LENGTH 8 SEC", (s["lspeed"], s["half"], s["len"]) == (2, 1, 1), s)
    p = pads(d, 2200)
    check("their pads light", all(p[i] == ON for i in (9, 10, 11, 12, 14, 15)) and p[13] == OFF, p[8:])
    s = state(d, 2700)
    check("ECHO off, LESLIE SLOW, REVERB on again at its own MIX (18)", (s["emix"], s["lspeed"], s["rmix"]) == (0, 1, 18), s)
    p = pads(d, 3000)
    check("a note sounding: the SOUND and the LAYER pads bright (active), the effects in it too",
          p[1] == ACT and p[6] == ACT and p[9] == ACT and p[13] == ACT and p[0] == OFF, p)

    print("== faders: two banks, a fader acts once it moves")
    out, d = run("faders")
    s = state(d, 400)
    check("faders at rest after power-on: nothing set", (s["bmix"], s["tone"], s["wow"], s["flut"]) == (50, 72, 28, 22), s)
    s = state(d, 1400)
    check("bank A: MIX 100, TONE 0, WOW 50, FLUTTER 100", (s["bmix"], s["tone"], s["wow"], s["flut"]) == (100, 0, 50, 100), s)
    check("FADER BANK: B", state(d, 1700)["bank"] == 1)
    s = state(d, 2100)
    check("bank B: a fader that does not move sets nothing; AGE 71, REVERB 50",
          (s["age"], s["sat"], s["emix"], s["rmix"]) == (71, 22, 0, 50) and s["tone"] == 0, s)
    s = state(d, 2400)
    check("back on A: TONE follows its fader again", s["bank"] == 0 and s["tone"] == 100, s)

    print("== wheels: the motor and the vibrato")
    out, d = run("wheels")
    check("at rest: no bend, no vibrato", state(d, 400)["bend"] == 0 and state(d, 400)["mod"] == 0)
    check("pitch wheel up: the motor up to +2 semitones (+3200 / 16 cents)", state(d, 900)["bend"] == 3200, state(d, 900))
    check("back to rest: 0", state(d, 1400)["bend"] == 0)
    check("down: -2 semitones", state(d, 1900)["bend"] == -3200, state(d, 1900))
    check("mod wheel up: the vibrato (as CC 1, 0..127)", 60 < state(d, 2900)["mod"] <= 127, state(d, 2900))
    check("mod wheel back: 0", state(d, 3400)["mod"] == 0)

    print("== knobs and pages")
    out, d = run("knobs")
    s = state(d, 600)
    check("K1 (KNOB 1) turns TONE, K4 (KNOB 4) RELEASE on the PLAY page; K8 PRESETS the sound",
          s["tone"] == 72 + 5 and s["sound"] == 1, s)
    check("> : the next page (LAYER)", state(d, 800)["page"] == 1)
    check("< < : round to the last page (SEQ)", state(d, 1100)["page"] == 11)
    check("FX, then K6 SELECT: the FX section's next page (LESLIE)", state(d, 1400)["page"] == 5, state(d, 1400))

    print("ALL SMK UI TESTS PASSED" if not fails else f"{fails} SMK UI TEST(S) FAILED")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
