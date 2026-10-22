#!/usr/bin/env bash
#
# Records what the Essentia-backed analysis produces for the test recordings,
# so the native analysis that replaces it can be checked against a reference
# without Essentia at test time. See Tests/AnalysisReferenceTests.cpp for the
# tolerances that comparison runs with.
#
# Each <name>.essentia.json holds, for one TestFiles/<name>.wav:
#
#   segments  SBic boundaries in seconds, first = 0, last = file duration
#   beats     Degara beat positions in seconds
#   bpm       the Degara tempo estimate
#   onsets    OnsetRate onsets in seconds
#
# The keys use the names the analysis types carry after the Essentia removal,
# not the CLI's legacy type names, so the fixtures survive that rename.
#
# Running it
# ----------
# Needs a CLI built against Essentia - no build has linked it since the
# analysis moved to the native segmenters, so this is the record of how the
# fixtures were made (check out a commit before that removal to run it) - and
# jq. Neither the build nor the test suite runs this - the committed JSON is
# the contract.
#
#   ./capture.sh ../../../../build/AudionautCli_artefacts/AudionautCli
#
set -euo pipefail

cli="${1:?usage: capture.sh <path to AudionautCli>}"
here="$(cd "$(dirname "$0")" && pwd)"
testFiles="$here/.."
repo="$here/../../../.."

producer="Essentia $(git -C "$repo/Submodules/essentia" describe --tags --always 2>/dev/null || echo unknown)"

for name in _export_TRK-18 TRK-18-epy-jul epy-oh-yeah-streicher-fix 120-funk-export; do
    audio="$testFiles/$name.wav"
    out="$here/$name.essentia.json"
    echo "$name"
    "$cli" analyze "$audio" --types sbic,beat_degara,onset --json 2>/dev/null \
      | jq --arg file "$name.wav" --arg producer "$producer" '
          .result.files[0].analyses
          | { file: $file,
              producer: $producer,
              segments: .sbic.segments,
              beats: .beat_degara.segments,
              bpm: .beat_degara.bpm,
              onsets: .onset.segments }' > "$out"
done
