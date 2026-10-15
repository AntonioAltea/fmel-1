#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build MEL's tapes (firmware/src/mel.c) from WAV files into a C header.

  tools/gen_tapes.py OUT.h [--src tapes/wav] [--sets MkII_Flute,8_Choir,...] [--budget BYTES] [--step 3] [--rate 22050]

The tapes are the Taijiguy Mellotron M400S samples (one WAV per key, G2..F5, ~7 s, named like
"A#3.wav"), in tapes/wav/<SET>/: they are not in this tree (free to use, not to redistribute in
commercial software). tapes/README.md says where they come from.

Per set, one root every STEP semitones (the keys between play the nearest root, at most STEP/2
semitones off), each:
  - from 44.1 kHz down to RATE >> SHIFT (the tapes hold almost nothing above 5 kHz: 11025 Hz, half of RATE, keeps
    it all);
  - from its onset (the head meeting the tape stays in), as long as the budget allows;
  - with a loop over the end, crossfaded in the samples (equal power), so a note can sound for
    ever (MEL's "inf" tape) as well as for the 8 s of a real tape;
  - IMA ADPCM, 4 bit, with the decoder's state every CK samples (MEL_CK): a note can start
    anywhere on its tape (a key pressed again before its tape is back) and the loop jumps exactly.
The levels of a set are evened out half way (in dB) between its roots; then every set is brought to the same
loudness (RMS of its sustain), so a change of tape is not a change of level.
A set that dies away by itself (DECAYS: the vibraphone) has its decay measured per root (dB/s, after its peak) and
flattened out of the samples, so its loop is level; the zone carries the decay and the firmware applies it as the
tape runs: a held note dies as the real one does, and a note restarted mid-tape starts as quiet as the tape is.
The piano (tools/get_piano.sh: VCSL, CC0) is not a Mellotron tape and is packed apart from the budget: every file
is a root (a key every half octave), short (SECONDS: under a second, then its loop) at the full RATE, 22050 Hz: 4-bit
ADPCM hisses over a piano's pure tones, ~8 dB less for every doubling of the rate (and a high key keeps its
partials). Its strike is tamed (KNOCK: the hammer's thump under the note; SPIKE: a high key's first 20 ms, which a
level match turned into a slap) and its keys evened out on their tone, all the way.
"""
import argparse
import re
import wave
from pathlib import Path

import numpy as np

NAMES = {"MkII_Violins": "VIOLINS", "MkII_Flute": "FLUTE", "8_Choir": "CHOIR", "Cello": "CELLO", "Vibes": "VIBES",
         "Woodwind_2": "WOODWIND", "Piano": "PIANO"}
SETS = ["MkII_Flute", "8_Choir", "Cello", "Vibes", "Piano"]  # A B C (the M400's three tracks), D, E
DECAYS = {"Vibes": 0.6, "Piano": 1.0}              # sets that die away by themselves, and their share of the flash:
                                                   # their decay is taken out of the samples (a flat loop) and
                                                   # measured; the firmware puts it back as the tape runs
SECONDS = {"Piano": (0.85, 0.5)}                   # sets of a fixed length (s, its lowest root .. its highest: a high
                                                   # string dies sooner), outside the budget, every file a root
SHIFT = {"Piano": 0}                               # sets at RATE >> SHIFT (the firmware steps them so); the rest at
SHIFT_ELSE = 1                                     # RATE >> 1: the tapes at 11025 Hz, the piano at 22050
STRUCK = {"Piano"}                                 # sets a key strikes anew: its tape from the start at every press
                                                   # (a tape's key pressed again goes on where its tape is: the
                                                   # vibraphone as quiet as it has died away to, a piano never)
SPIKE = {"Piano": (12, 6)}                         # sets whose strike's spike is held down (dB over their tone, at
                                                   # the lowest root .. the highest)
KNOCK = {"Piano": (3, 12)}                         # sets whose hammer knock is turned down (dB at the lowest root ..
                                                   # the highest: on a high key it is all you hear of the strike)
SOFT = {"Piano"}                                   # sets whose attack peaks over the common loudness: a soft clip
                                                   # (as a tape driven hard) takes them under full scale
TARGET_RMS = 0.13                                # every set's sustain, full scale 1
NOTE = {"C": 0, "C#": 1, "D": 2, "D#": 3, "E": 4, "F": 5, "F#": 6, "G": 7, "G#": 8, "A": 9, "A#": 10, "B": 11}
CK = 256                                         # samples between decoder states
SRC_RATE = 44100

IMA_STEP = [
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97,
    107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
    876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428,
    4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350,
    22385, 24623, 27086, 29794, 32767]
IMA_IDX = [-1, -1, -1, -1, 2, 4, 6, 8]


def midi_of(name):
    m = re.fullmatch(r"([A-G]#?)(-?\d)", name)
    return 12 * (int(m[2]) + 1) + NOTE[m[1]] if m else None


def read_wav(path):
    with wave.open(str(path)) as w:
        if w.getsampwidth() != 2 or w.getframerate() != SRC_RATE:
            raise SystemExit(f"{path}: want 16-bit {SRC_RATE} Hz")
        x = np.frombuffer(w.readframes(w.getnframes()), dtype="<i2").astype(np.float64) / 32768
        return x[::w.getnchannels()]


def lowpass(fc, taps=255):
    """windowed sinc, fc in cycles per sample"""
    n = np.arange(taps) - (taps - 1) / 2
    h = 2 * fc * np.sinc(2 * fc * n) * np.blackman(taps)
    return h / h.sum()


def resample(x, rate, sh=0):
    if SRC_RATE % rate:
        raise SystemExit(f"--rate must divide {SRC_RATE}")
    d = SRC_RATE // rate << sh
    return np.convolve(x, lowpass(0.45 / d), mode="same")[::d] if d > 1 else x


def onset(x, rel=0.08):
    a = np.abs(x)
    env = np.convolve(a, np.ones(32) / 32, mode="same")
    return int(np.argmax(env > rel * env.max()))


def rms(x):
    return float(np.sqrt(np.mean(x * x)) + 1e-12)


def loop(x, rate, short=False):
    """x with a crossfaded loop over its end -> (samples, loop start); the loop is [ls, len). short (a piano key, under
    a second): a shorter crossfade, its loop after the strike (from 0.12 s) and over the second half"""
    n = len(x)
    xf = int(min(0.35 * rate, 0.25 * n) if short else 0.35 * rate)
    target = max(xf + CK, int(n * 0.3), xf + int(0.12 * rate) if short else 0)
    hi = n - xf - CK if short else int(n * 0.6) - xf
    best, ls = -2.0, target - target % CK
    for cand in range(target - target % CK, hi, CK):   # ls on the CK grid: its state is a checkpoint
        a, b = x[n - xf:], x[cand - xf:cand]
        c = float(np.dot(a, b) / (np.linalg.norm(a) * np.linalg.norm(b) + 1e-12))
        if c > best:
            best, ls = c, cand
    t = np.linspace(0, np.pi / 2, xf)
    y = x.copy()
    y[n - xf:] = x[n - xf:] * np.cos(t) + x[ls - xf:ls] * np.sin(t)    # at the end it has become what precedes ls
    return y, ls


def tame_knock(x, rate, db, midi):
    """the hammer's knock down by db: in the first 40 ms (back to as it was by 120 ms) whatever is not one of the
    note's partials (each found near k f0, a piano's a little sharp, where it sustains: 0.15-0.45 s in) is turned
    down; the partials stay as they are"""
    n, hop = 1024, 128
    w = np.hanning(n + 1)[:n]
    late = x[int(0.15 * rate):int(0.45 * rate)]
    L = np.abs(np.fft.rfft(late[:n * (len(late) // n)].reshape(-1, n) * w, axis=1)).mean(axis=0)
    f, f0 = np.fft.rfftfreq(n, 1 / rate), 440 * 2 ** ((midi - 69) / 12)
    keep = np.zeros(len(f), bool)
    for k in range(1, int(rate / 2 / f0) + 1):
        near = np.where((f > k * f0 * 0.97) & (f < k * f0 * 1.08))[0]
        if len(near):
            j = near[np.argmax(L[near])]
            keep[max(0, j - 2):j + 3] = True    # the partial and 2 bins either side
    pad = np.concatenate([np.zeros(n), x, np.zeros(n)])
    y, norm = np.zeros(len(pad)), np.zeros(len(pad))
    for i in range(0, len(pad) - n, hop):
        t = (i + n / 2 - n) / rate              # the frame's middle, s into x
        a = 1.0 if t > 0.12 else 10 ** (-db / 20) if t < 0.04 else 10 ** (-db * (0.12 - t) / 0.08 / 20)
        F = np.fft.rfft(pad[i:i + n] * w)
        F[~keep] *= a
        y[i:i + n] += np.fft.irfft(F, n) * w
        norm[i:i + n] += w * w
    return (y / np.maximum(norm, 1e-9))[n:n + len(x)]


def soften_strike(x, rate, cap_db):
    """the strike's first spike held to cap_db over the tone 0.1-0.2 s in (a high key's is all of its first 20 ms:
    under a hard level match it came out as a slap); a slow gain (5 ms), gone by 0.15 s"""
    w = max(1, int(0.004 * rate))
    env = np.sqrt(np.convolve(x * x, np.ones(w) / w, mode="same")) + 1e-9
    limit = rms(x[int(0.1 * rate):int(0.2 * rate)]) * 10 ** (cap_db / 20)
    gain = np.minimum(1.0, limit / env)
    k = np.hanning(2 * w + 1)
    gain = np.minimum(gain, np.convolve(gain, k / k.sum(), mode="same"))   # (no faster than the window)
    gain = np.convolve(gain, k / k.sum(), mode="same")
    t = np.arange(len(x)) / rate
    fade = np.clip((0.15 - t) / 0.05, 0, 1)    # full up to 0.1 s, none from 0.15 s
    return x * (1 - fade + fade * gain)


def ima_encode(s):
    """int16 samples -> (bytes, [(pred, idx) at every CK-th sample]); low nibble first"""
    pred, idx, out, cks, cur = 0, 0, bytearray(), [], 0
    for i, v in enumerate(s):
        if i % CK == 0:
            cks.append((pred, idx))
        step = IMA_STEP[idx]
        d = int(v) - pred
        code = 8 if d < 0 else 0
        d = abs(d)
        vd = step >> 3
        if d >= step:
            code |= 4; d -= step; vd += step
        if d >= step >> 1:
            code |= 2; d -= step >> 1; vd += step >> 1
        if d >= step >> 2:
            code |= 1; vd += step >> 2
        pred = max(-32768, min(32767, pred - vd if code & 8 else pred + vd))
        idx = max(0, min(88, idx + IMA_IDX[code & 7]))
        if i & 1:
            out.append(cur | code << 4)
        else:
            cur = code
    if len(s) & 1:
        out.append(cur)
    return bytes(out), cks


def decay_of(x, rate):
    """(peak sample, decay in dB/s after it) of a sound that dies away"""
    w = max(1, int(0.05 * rate))
    env = np.sqrt(np.convolve(x * x, np.ones(w) / w, mode="same")) + 1e-9
    pk = int(np.argmax(env[:int(0.5 * rate)]))
    a, b = pk + int(0.1 * rate), min(len(x), pk + int(3.0 * rate))
    t = np.arange(a, b) / rate
    db = 20 * np.log10(env[a:b])
    slope = -np.polyfit(t, db, 1)[0] if b - a > w * 4 else 6.0
    return pk, float(min(40.0, max(1.0, slope)))


def build(src, budget, step, rate, which):
    sets = [(d, NAMES.get(d, d.upper())) for d in which if (src / d).is_dir()]
    if not sets:
        raise SystemExit(f"no tapes in {src} (see tapes/README.md)")
    plan = []
    for d, name in sets:
        files = {midi_of(p.stem): p for p in (src / d).glob("*.wav") if midi_of(p.stem) is not None}
        lo, hi = min(files), max(files)
        roots = [r for r in range(lo + step // 2, hi + 1, step)]
        if roots[-1] + step // 2 < hi:
            roots.append(hi)
        if d in SECONDS:
            roots = sorted(files)
        plan.append((name, files, roots))
    shares = [0.0 if d in SECONDS else DECAYS.get(d, 1.0) for d, _ in sets]
    nroots = sum(len(r) * w for (_, _, r), w in zip(plan, shares))
    per0 = budget * 2 / nroots                   # 4 bits a sample (+ the checkpoints, ~1.6 %)
    per0 = per0 / (1 + 8 / CK)
    out = {"sets": [], "zones": [], "data": bytearray(), "ck": []}
    for (d, _), (name, files, roots), share in zip(sets, plan, shares):
        segs, decs = [], []
        sh = SHIFT.get(d, SHIFT_ELSE)
        zr = SRC_RATE / (SRC_RATE // rate << sh)    # this set's rate, Hz
        per = int(per0 * share) // CK * CK
        for i, r in enumerate(roots):
            if d in SECONDS:
                a, b = SECONDS[d]
                per = int((a + (b - a) * i / max(1, len(roots) - 1)) * zr) // CK * CK
            x = resample(read_wav(files[r]), rate, sh)
            o = max(0, onset(x) - int(0.004 * zr))
            seg = x[o:o + per]
            if d in KNOCK:
                a, b = KNOCK[d]
                seg = tame_knock(seg, zr, a + (b - a) * i / max(1, len(roots) - 1), r)
            if d in SPIKE:
                a, b = SPIKE[d]
                seg = soften_strike(seg, zr, a + (b - a) * i / max(1, len(roots) - 1))
            if d in DECAYS:                      # take the decay out: a level loop
                pk, dbs = decay_of(x[o:], zr)
                t = np.maximum(0, np.arange(len(seg)) - pk) / zr
                seg = seg * 10 ** (dbs * t / 20)
                decs.append((pk, dbs))
            else:
                decs.append((0, 0.0))
            segs.append((r, seg))
        if d in SECONDS:                         # the piano: its keys as loud as each other where it is heard, the
            lv = np.array([20 * np.log10(rms(s[int(0.03 * zr):int(0.3 * zr)])) for _, s in segs])   # strike, all
                                                 # the way (after its first 30 ms: the hammer's knock is no measure)
        else:
            lv = np.array([20 * np.log10(rms(s[len(s) // 3:])) for _, s in segs])
        z0 = len(out["zones"])
        gains = 10 ** ((lv.mean() - lv) / (1 if d in SECONDS else 2) / 20)   # (the tapes: half way to their mean)
        segs = [(r, s * g) for (r, s), g in zip(segs, gains)]
        level = np.sqrt(np.mean([rms(s[len(s) // 3:]) ** 2 for _, s in segs]))
        segs = [(r, s * TARGET_RMS / level) for r, s in segs]      # every set as loud as the others
        if d in SOFT:                            # linear to 0.6, then a tanh to 0.95
            segs = [(r, np.where(np.abs(s) < 0.6, s, np.sign(s) * (0.6 + 0.35 * np.tanh((np.abs(s) - 0.6) / 0.35))))
                    for r, s in segs]
        peak = max(np.abs(s).max() for _, s in segs)
        if peak > 0.95:
            raise SystemExit(f"{name}: peaks at {peak:.2f} at the common loudness; lower TARGET_RMS")
        for i, (r, s) in enumerate(segs):
            s, ls = loop(s, zr, d in SECONDS)
            pcm = np.clip(np.round(s * 32767), -32767, 32767).astype(np.int16)
            data, cks = ima_encode(pcm)
            lo = roots[i - 1] + (r - roots[i - 1]) // 2 + 1 if i else 0
            hi = r + (roots[i + 1] - r) // 2 if i + 1 < len(roots) else 127
            pk, dbs = decs[i]
            dk = round((1 - 10 ** (-dbs / 20 / zr)) * (1 << 24)) if dbs else 0       # per tape sample, Q24
            out["zones"].append(dict(off=len(out["data"]), n=len(pcm), ls=ls, ck=len(out["ck"]), root=r, lo=lo, hi=hi,
                                     sh=sh, dk=dk, d1k=round(32767 * 10 ** (-dbs * 1024 / zr / 20)), pk=pk))
            out["data"] += data
            out["ck"] += cks
        out["sets"].append((name, z0, len(roots), d in STRUCK))
    return out, int(per0) // CK * CK


def tables():
    """the firmware has no float: its curves, computed here"""
    def arr(decl, vals, per_line=10):
        L = [decl + " = {"]
        for i in range(0, len(vals), per_line):
            L.append("    " + " ".join(f"{v}u," for v in vals[i:i + per_line]))
        return L + ["};"]
    pow2 = [round(2 ** 30 * 2 ** (c / 1200)) for c in range(1201)]
    tone = []                                    # TONE 0..100: a one-pole's coefficient, 350 Hz .. 16 kHz
    for v in range(101):
        fc = 350 * (16000 / 350) ** (v / 100)
        tone.append(round(32768 * (1 - np.exp(-2 * np.pi * fc / SRC_RATE))))
    sat = []                                     # tape saturation: |x| 0..2 full scale -> 0..1, linear up to 0.6
    for i in range(257):
        x = 2 * i / 256
        y = x if x < 0.6 else 0.6 + 0.4 * np.tanh((x - 0.6) / 0.4)
        sat.append(round(32767 * min(y, 1)))
    atk = []                                     # ATTACK 0..100: 3 ms .. 2 s, the rise per 32 frames, Q24; as v^1.5, so
    for v in range(101):                         # the knob is felt from ~10 on (10: 66 ms, 20: 0.18 s, 50: 0.71 s; an
        t = 0.003 + 2.0 * (v / 100) ** 1.5       # exponential spent its first 40 under the tapes' own rise, 40-70 ms)
        atk.append(min(1 << 24, round((1 << 24) * 32 / (t * SRC_RATE))))
    lfo = [round(0.5 * 20 ** (v / 100) * 2 ** 32 / SRC_RATE) for v in range(101)]   # VIBRATO RATE: 0.5 .. 10 Hz
    lfo10 = [round(0.5 * 20 ** (v / 100) * 10) for v in range(101)]
    eq_hz = [int(round(200 * 25 ** (i / 15), -1 if i < 9 else -2)) for i in range(16)]   # EQ FREQ: 200 Hz .. 5 kHz
    svf = [round(16384 * 2 * np.sin(np.pi * f / SRC_RATE)) for f in eq_hz]
    rel = []                                     # REL 0..100: 20 ms .. 6 s to -60 dB, per 32 frames, Q16
    for v in range(101):
        t = 0.02 * 300 ** (v / 100)
        rel.append(round(65536 * (1 - np.exp(-6.9 * 32 / (SRC_RATE * t)))))
    def onepole(fc):
        return round(32768 * (1 - np.exp(-2 * np.pi * fc / SRC_RATE)))
    genlp = [onepole(9000 * (1100 / 9000) ** (v / 100)) for v in range(101)]   # AGE: a copy of a copy of a copy
    genhp = [onepole(20 * (380 / 20) ** (v / 100)) for v in range(101)]         # (two poles each: mel_fx.c)
    # DRIVE, after the JHS 424 (a Tascam Portastudio's cassette preamp): 0 dB .. +36 dB into a soft clipper that keeps
    # going (linear to half scale, then a tanh that never quite gets there), a bias for the even harmonics, a low-pass
    # that closes as it drives (warm, not fizzy), and the level given back so a turn of DRIVE is not a turn of volume
    def clip(x):
        x = np.abs(x)
        return np.where(x < 0.5, x, 0.5 + 0.5 * np.tanh((x - 0.5) / 0.5))
    clipt = [round(32767 * float(clip(np.array([i * 1024 / 32768]))[0])) for i in range(257)]   # |u| 0 .. 8 x full
    drv, drvmk, drvlp = [], [], []
    tt = np.arange(4410) / SRC_RATE
    sig = 0.16 * np.sin(2 * np.pi * 220 * tt)   # a note at -19 dBFS RMS, as the tapes play
    for v in range(101):
        g = 10 ** (36 * (v / 100) ** 1.4 / 20)
        b = 0.15 * v / 100
        u = sig * g + b
        y = np.sign(u) * clip(u)
        y = y - y.mean()
        mk = min(1.0, max(0.08, np.sqrt(np.mean(sig ** 2)) / np.sqrt(np.mean(y ** 2))))
        drv.append(round(256 * g))
        drvmk.append(round(32767 * mk))
        drvlp.append(onepole(12000 * (4500 / 12000) ** (v / 100)))
    return (arr("static const uint32_t MEL_POW2[1201]   /* 2^(c/1200), Q30 */", pow2, 6) +
            arr("static const uint16_t MEL_CLIP[257]    /* DRIVE's clipper: |u| / 1024 (to 8 x full scale) -> Q15 */", clipt) +
            arr("static const uint16_t MEL_DRV[101]     /* DRIVE -> its gain, Q8 (0 .. +36 dB) */", drv) +
            arr("static const uint16_t MEL_DRVMK[101]   /* -> the level given back, Q15 */", drvmk) +
            arr("static const uint16_t MEL_DRVLP[101]   /* -> its low-pass, 12 .. 4.5 kHz, Q15 */", drvlp) +
            arr("static const uint32_t MEL_ATK[101]     /* ATTACK -> the rise per 32 frames, Q24 */", atk) +
            arr("static const uint32_t MEL_LFO[101]     /* VIBRATO RATE -> phase per frame */", lfo, 6) +
            arr("static const uint8_t MEL_LFO_HZ10[101] /* (its Hz x 10, shown) */", lfo10) +
            arr("static const uint16_t MEL_EQ_HZ[16]    /* EQ FREQ, Hz */", eq_hz) +
            arr("static const uint16_t MEL_SVF[16]      /* its state-variable filter's f = 2 sin(pi fc / fs), Q14 */", svf) +
            arr("static const uint16_t MEL_GENLP[101]   /* AGE -> the tape's top end, 9 kHz .. 1.1 kHz, Q15 */", genlp) +
            arr("static const uint16_t MEL_GENHP[101]   /* AGE -> its bottom end, 20 .. 380 Hz, Q15 */", genhp) +
            arr("static const uint16_t MEL_REL[101]     /* REL -> decay per 32 frames, Q16 */", rel) +
            arr("static const uint16_t MEL_TONE[101]    /* TONE -> one-pole coefficient, Q15 */", tone) +
            arr("static const uint16_t MEL_SAT[257]     /* saturation: |x| / 2^8 of 2 x full scale -> Q15 */", sat))


def write(out, path, rate, per):
    z, data = out["zones"], out["data"]
    L = ["/* generated by tools/gen_tapes.py: MEL's tapes (Taijiguy's Mellotron M400S samples; the piano, VCSL's (CC0):"
         " README.md, LICENSING.md) */",
         f"#define MEL_RATE {rate}u",
         f"#define MEL_RATE_Q16 {rate * 65536 // 44100}u          /* MEL_RATE / 44100, Q16 (a zone's: >> its sh) */",
         f"#define MEL_CK {CK}u",
         f"#define MEL_NSETS {len(out['sets'])}u",
         f"#define MEL_NZONES {len(z)}u",
         f"/* {len(z)} zones, the tapes' of {per} samples ({per / (rate >> SHIFT_ELSE):.2f} s), {len(data)} B of ADPCM, "
         f"{4 * len(out['ck'])} B of checkpoints */",
         "static const char *const MEL_SET_NAME[MEL_NSETS] = {" + ", ".join(f'"{n}"' for n, _, _, _ in out["sets"]) + "};",
         "static const uint8_t MEL_SET_Z0[MEL_NSETS] = {" + ", ".join(str(a) for _, a, _, _ in out["sets"]) + "};",
         "static const uint8_t MEL_SET_NZ[MEL_NSETS] = {" + ", ".join(str(b) for _, _, b, _ in out["sets"]) + "};",
         "static const uint8_t MEL_SET_STRUCK[MEL_NSETS] = {" + ", ".join(str(int(k)) for *_, k in out["sets"]) + "};",
         "static const mel_zone_t MEL_ZONE[MEL_NZONES] = {"]
    for q in z:
        L.append(f"    {{{q['off']}u, {q['n']}u, {q['ls']}u, {q['ck']}u, {q['root']}, {q['lo']}, {q['hi']}, {q['sh']}, "
                 f"{q['dk']}u, {q['d1k']}u, {q['pk']}u}},")
    L.append("};")
    L.append("static const uint32_t MEL_CKS[] = {   /* (uint16_t)predictor | step index << 16 */")
    for i in range(0, len(out["ck"]), 8):
        L.append("    " + " ".join(f"0x{(p & 0xFFFF) | (k << 16):06x}u," for p, k in out["ck"][i:i + 8]))
    L.append("};")
    L += tables()
    L.append("static const uint8_t MEL_DATA[] = {")
    for i in range(0, len(data), 24):
        L.append("    " + ",".join(str(b) for b in data[i:i + 24]) + ",")
    L.append("};")
    Path(path).write_text("\n".join(L) + "\n")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("out")
    ap.add_argument("--src", default=str(Path(__file__).resolve().parents[1] / "tapes" / "wav"))
    ap.add_argument("--sets", default=",".join(SETS), help="the tapes, in order (A, B, C, ...): folders of --src")
    ap.add_argument("--budget", type=int, default=425_000, help="bytes of tape data (ADPCM + checkpoints) in all")
    ap.add_argument("--step", type=int, default=3, help="semitones between roots")
    ap.add_argument("--rate", type=int, default=22050, help="the firmware's rate (MEL_RATE): a set's is it >> SHIFT")
    a = ap.parse_args()
    out, per = build(Path(a.src), a.budget, a.step, a.rate, a.sets.split(","))
    Path(a.out).parent.mkdir(parents=True, exist_ok=True)
    write(out, a.out, a.rate, per)
    print(f"tapes: {', '.join(n for n, *_ in out['sets'])}: {len(out['zones'])} zones of {per / (a.rate >> SHIFT_ELSE):.2f} s, "
          f"{len(out['data']) + 4 * len(out['ck'])} B -> {a.out}")


if __name__ == "__main__":
    main()
