#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# MEL's tapes: Taijiguy's (Bernie Kornowicz) Mellotron M400S samples, one WAV per key (G2..F5), from his old site
# leisureland.us as the Wayback Machine keeps it (free to use, not in commercial software: README.md). ~107 MB. Only to
# make firmware/gen/mel_tapes.h again (tools/gen_tapes.py): the build takes that, in the tree.
#   tools/get_tapes.sh          ->  tapes/zip/*.zip, tapes/wav/<SET>/*.wav
set -e
cd "$(dirname "$0")/.."
mkdir -p tapes/zip tapes/wav
BASE="http://www.leisureland.us/audio/MellotronSamples"
for e in "20230210165402 MkII%20Violins MkII_Violins" "20230210170717 MkII%20Flute MkII_Flute" \
         "20230210165719 8%20Choir 8_Choir" "20240717141134 Cello Cello" "20230210165732 Vibes Vibes"; do
    set -- $e
    [ -s "tapes/zip/$3.zip" ] || curl -sSLf --retry 3 -o "tapes/zip/$3.zip" "https://web.archive.org/web/${1}id_/$BASE/$2.zip"
    mkdir -p "tapes/wav/$3"
    unzip -qo "tapes/zip/$3.zip" -d "tapes/wav/$3"
    echo "tapes: $3 ($(ls "tapes/wav/$3" | wc -l | tr -d ' ') WAV)"
done
