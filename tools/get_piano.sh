#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# MEL's piano: SLOOP's (isod89/sloop-fm1, assets/samples-cc0/PIANO), four keys of a Steinway B from Versilian Studios'
# VCSL (Sam Gossner), CC0 1.0 (public domain: LICENSING.md), as SLOOP cut them (mono, 44.1 kHz): C3, G#3, E4, C5.
# tools/gen_tapes.py processes them as SLOOP's gen_samples.py does ("piano"). Only to make firmware/gen/mel_tapes.h
# again: the build takes that, in the tree.
#   tools/get_piano.sh          ->  tapes/wav/Piano/{C3,G#3,E4,C5}.wav
set -e
cd "$(dirname "$0")/.."
SLOOP=ba54ba5f0e7f6e0a1a3499497809e1e6c1ad7e8c   # (the commit they are taken from)
BASE="https://raw.githubusercontent.com/isod89/sloop-fm1/$SLOOP/assets/samples-cc0/PIANO"
mkdir -p tapes/wav/Piano
rm -f tapes/wav/Piano/*.wav                     # (only these keys: gen_tapes.py takes every file there)
for e in "00_C3_m48 C3" "01_Gs3_m56 G#3" "02_E4_m64 E4" "03_C5_m72 C5"; do
    set -- $e
    curl -sSLf --retry 3 -o "tapes/wav/Piano/$2.wav" "$BASE/$1.wav"
done
echo "piano: $(ls tapes/wav/Piano | wc -l | tr -d ' ') WAV"
