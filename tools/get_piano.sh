#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# MEL's piano: a Kawai grand from Versilian Studios' VCSL (Sam Gossner), CC0 1.0 (public domain: LICENSING.md), a
# key every half octave from F#3 to F#6 (the keyboard and an octave up: a piano is played high), at a medium touch (v3:
# brighter than v2, and recorded 11-22 dB louder, so its own noise floor is that much further down). VCSL names its notes an octave low (C3 is
# middle C): they are named here as the tapes are (C4 = MIDI 60). ~25 MB. Only to make firmware/gen/mel_tapes.h again
# (tools/gen_tapes.py, --sets ...,Piano): the build takes that, in the tree. Needs ffmpeg (the files are stereo).
#   tools/get_piano.sh          ->  tapes/piano/*_v3.wav (as VCSL has them), tapes/wav/Piano/*.wav (44.1 kHz, 16 bit, mono)
set -e
cd "$(dirname "$0")/.."
mkdir -p tapes/piano tapes/wav/Piano
rm -f tapes/wav/Piano/*.wav                     # (only these keys: gen_tapes.py takes every file there)
BASE="https://raw.githubusercontent.com/sgossner/VCSL/master/Chordophones/Zithers/Grand%20Piano,%20Kawai/Sustains"
for e in "F%232 F#3" "C3 C4" "F%233 F#4" "C4 C5" "F%234 F#5" "C5 C6" "F%235 F#6"; do
    set -- $e
    f="tapes/piano/$(printf '%s' "$1" | sed 's/%23/#/')_v3.wav"
    [ -s "$f" ] || curl -sSLf --retry 3 -o "$f" "$BASE/GPiano_sus_${1}_v3_rr1_Player.wav"
    ffmpeg -loglevel error -y -i "$f" -ac 1 -ar 44100 -c:a pcm_s16le "tapes/wav/Piano/$2.wav"
done
echo "piano: $(ls tapes/wav/Piano | wc -l | tr -d ' ') WAV"
