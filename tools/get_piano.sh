#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# MEL's piano: a Kawai grand from Versilian Studios' VCSL (Sam Gossner), CC0 1.0 (public domain: LICENSING.md), a
# key every half octave at a soft touch (v2: mellow, as a lo-fi piano). VCSL names its notes an octave low (C3 is
# middle C): they are named here as the tapes are (C4 = MIDI 60). ~25 MB. Only to make firmware/gen/mel_tapes.h again
# (tools/gen_tapes.py, --sets ...,Piano): the build takes that, in the tree. Needs ffmpeg (the files are stereo).
#   tools/get_piano.sh          ->  tapes/piano/*.wav (as VCSL has them), tapes/wav/Piano/*.wav (44.1 kHz, 16 bit, mono)
set -e
cd "$(dirname "$0")/.."
mkdir -p tapes/piano tapes/wav/Piano
BASE="https://raw.githubusercontent.com/sgossner/VCSL/master/Chordophones/Zithers/Grand%20Piano,%20Kawai/Sustains"
for e in "A1 A2" "E2 E3" "A%232 A#3" "E3 E4" "A%233 A#4" "E4 E5"; do
    set -- $e
    f="tapes/piano/$(printf '%s' "$1" | sed 's/%23/#/').wav"
    [ -s "$f" ] || curl -sSLf --retry 3 -o "$f" "$BASE/GPiano_sus_${1}_v2_rr1_Player.wav"
    ffmpeg -loglevel error -y -i "$f" -ac 1 -ar 44100 -c:a pcm_s16le "tapes/wav/Piano/$2.wav"
done
echo "piano: $(ls tapes/wav/Piano | wc -l | tr -d ' ') WAV"
