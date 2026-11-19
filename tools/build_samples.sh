#!/usr/bin/env bash
# Rebuilds the included sample sets (src/samples/*.h and data/*.bin) from the
# original recordings: downloads them into a work directory, prepares the SFZ
# mappings and converts them with tools/sfz2header.py. The sources and the
# settings are described in docs/sample-sets.md.
#
# Usage: tools/build_samples.sh [-w WORK_DIR] [INSTRUMENT ...]
#   INSTRUMENT: upright-piano grand-piano epiano drums sax clarinet guitar
#               bass upright-bass (default: all)
#   WORK_DIR:   download directory (default: ~/.cache/tinyinstruments-sources);
#               downloads which are already there are not repeated
#
# Requirements: python3 with numpy, ffmpeg, curl, 7z, unzip, git
set -euo pipefail

LIB=$(cd "$(dirname "$0")/.." && pwd)
WORK=${TI_SAMPLES_WORK:-$HOME/.cache/tinyinstruments-sources}
if [ "${1:-}" = "-w" ]; then
  WORK=$2
  shift 2
fi
mkdir -p "$WORK"
CONV=(python3 "$LIB/tools/sfz2header.py")
F2S=(python3 "$LIB/tools/files2sfz.py")
UA="User-Agent: tinyinstruments-build"
VCSL=https://raw.githubusercontent.com/sgossner/VCSL/master
VSCO=https://raw.githubusercontent.com/sgossner/VSCO-2-CE/master

# convert NAME SFZ LICENSE FULL_OPTIONS SMALL_OPTIONS [OCTAVE_OPTIONS]
# full: 700 KB, 32 kHz; small: 250 KB, 22.05 kHz; octave: 150 KB, 22.05 kHz,
# one recording per octave (only if OCTAVE_OPTIONS is given)
convert() {
  local name=$1 sfz=$2 lic=$3 full=$4 small=$5 octave=${6-}
  echo "== $name"
  # shellcheck disable=SC2086
  "${CONV[@]}" "$sfz" --name "$name" $full --budget 700000 --license "$lic" \
    -o "$LIB/src/samples/$name.h" --binary "$LIB/data/$name.bin" | tail -1
  # shellcheck disable=SC2086
  "${CONV[@]}" "$sfz" --name "${name}Small" $small --rate 22050 --budget 250000 --license "$lic" \
    -o "$LIB/src/samples/${name}Small.h" --binary "$LIB/data/${name}Small.bin" | tail -1
  if [ -n "$octave" ]; then
    # shellcheck disable=SC2086
    "${CONV[@]}" "$sfz" --name "${name}Octave" $octave --step 12 --rate 22050 --budget 150000 \
      --license "$lic" -o "$LIB/src/samples/${name}Octave.h" --binary "$LIB/data/${name}Octave.bin" | tail -1
  fi
}

# downloads the first BYTES bytes of each file of LIST (paths in REPO) into DIR:
# only the beginning of the long recordings is needed
download_ranged() {
  local base=$1 list=$2 dir=$3 bytes=$4
  mkdir -p "$dir"
  while read -r f; do
    local out="$dir/$(basename "$f")"
    [ -s "$out" ] && continue
    local url="$base/$(python3 -c "import urllib.parse,sys;print(urllib.parse.quote(sys.argv[1]))" "$f")"
    curl -sSf -r "0-$((bytes - 1))" -o "$out" "$url" &
    while [ "$(jobs -r | wc -l)" -ge 8 ]; do wait -n; done
  done < "$list"
  wait
}

# lists the files of a GitHub repository (git tree) whose path starts with PREFIX
github_files() {
  local repo=$1 prefix=$2 tree="$WORK/$(echo "$1" | tr / _)-tree.json"
  [ -s "$tree" ] || curl -sSf -H "$UA" "https://api.github.com/repos/$repo/git/trees/master?recursive=1" -o "$tree"
  python3 - "$tree" "$prefix" <<'EOF'
import json, sys
for x in json.load(open(sys.argv[1]))["tree"]:
    if x["type"] == "blob" and x["path"].startswith(sys.argv[2]) and x["path"].endswith(".wav"):
        print(x["path"])
EOF
}

upright_piano() {
  local d="$WORK/upright-piano"
  mkdir -p "$d"
  if [ ! -d "$d/extract" ]; then
    curl -sSfL -o "$d/kw.7z" "https://freepats.zenvoid.org/Piano/UprightPianoKW/UprightPianoKW-SFZ+FLAC-20220221.7z"
    7z x -y -o"$d/extract" "$d/kw.7z" > /dev/null
  fi
  convert UprightPianoKW "$d/extract/UprightPianoKW-SFZ+FLAC-20220221/UprightPianoKW-20220221.sfz" \
    "CC0 1.0 (FreePats Upright Piano KW, http://freepats.zenvoid.org)" \
    "--mode keyboard" "--mode keyboard --single-layer" "--mode keyboard --single-layer"
}

grand_piano() {
  local d="$WORK/grand-piano"
  mkdir -p "$d"
  # Steinway B, no sustain pedal, close microphones: every second recorded
  # note and the highest one, all velocity layers, first round robin
  github_files sgossner/VCSL "Chordophones/Zithers/Grand Piano, Steinway B/NoSus/JHPiano_NoSus_Close_" > "$d/all.txt"
  python3 - "$d/all.txt" > "$d/files.txt" <<'EOF'
import re, sys
names = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}
def num(path):
    m = re.search(r"_([A-G])(#?)(-?\d)_vl", path)
    return names[m.group(1)] + (1 if m.group(2) else 0) + (int(m.group(3)) + 1) * 12
paths = [p for p in open(sys.argv[1]).read().split("\n") if p]
notes = sorted(set(num(p) for p in paths))
keep = set(notes[::2]) | {notes[-1]}
print("\n".join(p for p in paths if num(p) in keep))
EOF
  download_ranged "$VCSL" "$d/files.txt" "$d/wav" 1500000
  "${F2S[@]}" "$d"/wav/*.wav -o "$d/grand.sfz" --pattern '_(?P<note>[A-G]#?-?\d)_vl(?P<vel>\d)_rr1'
  convert GrandPiano "$d/grand.sfz" "CC0 1.0 (VCSL Grand Piano, Steinway B, https://github.com/sgossner/VCSL)" \
    "--mode keyboard" "--mode keyboard --single-layer" "--mode keyboard --single-layer"
}

epiano() {
  local d="$WORK/epiano"
  [ -d "$d/GregSullivan.E-Pianos" ] || git clone -q --depth 1 https://github.com/sfzinstruments/GregSullivan.E-Pianos.git "$d/GregSullivan.E-Pianos"
  convert Wurlitzer "$d/GregSullivan.E-Pianos/Wurlitzer EP200/Wurlitzer EP200.sfz" \
    "CC-BY 3.0 (Greg Sullivan, Wurlitzer EP200 v1.1, http://www.sullivang.net/; SFZ mapping by kinwie, https://github.com/sfzinstruments/GregSullivan.E-Pianos)" \
    "" "--single-layer" "--single-layer"
}

drums() {
  local d="$WORK/drums"
  mkdir -p "$d"
  if [ ! -d "$d/extract" ]; then
    curl -sSfL -o "$d/vd.zip" https://versilian-studios.com/Distro/Virtuosity_Drums_v0.925.zip
    unzip -q "$d/vd.zip" -d "$d/extract"
  fi
  local opts="--mode natural --keys 35-84 --choke 46:42,44 --normalize-layers"
  convert DrumKit "$d/extract/Programs/06-mid-mic.sfz" "CC0 1.0 (Versilian Studios Virtuosity Drums, mid mic)" \
    "$opts --max-layers 2" "$opts --single-layer"
}

sax() {
  local d="$WORK/sax"
  mkdir -p "$d"
  # tenor saxophone without vibrato (the player adds the vibrato)
  github_files sgossner/VCSL "Aerophones/Reed Aerophones/Tenor Saxophone/Non-Vibrato/" > "$d/files.txt"
  download_ranged "$VCSL" "$d/files.txt" "$d/wav" 900000
  # the files are named one octave too low
  "${F2S[@]}" "$d"/wav/*.wav -o "$d/sax.sfz" --pattern '_Main_(?P<note>[A-G]#?-?\d)_vl(?P<vel>\d)_rr1' \
    --transpose 12 --lokey 36 --hikey 96
  local opts="--mode sustain --loop-fraction 0.65 --normalize-layers --tune-correct --pitch-marks"
  convert TenorSax "$d/sax.sfz" "CC0 1.0 (VCSL Tenor Saxophone, non vibrato, https://github.com/sgossner/VCSL)" \
    "$opts" "$opts --single-layer" "$opts --single-layer"
}

clarinet() {
  local d="$WORK/clarinet"
  mkdir -p "$d"
  github_files sgossner/VSCO-2-CE "Woodwinds/Clarinet/susLong/" > "$d/files.txt"
  download_ranged "$VSCO" "$d/files.txt" "$d/wav" 1200000
  # the files are named one octave too low
  "${F2S[@]}" "$d"/wav/*.wav -o "$d/clarinet.sfz" --pattern '_susLong_(?P<note>[A-G]#?-?\d)_v(?P<vel>\d)_rr1' \
    --transpose 12 --lokey 48 --hikey 96
  local opts="--mode sustain --loop-fraction 0.65 --normalize-layers --tune-correct --pitch-marks"
  convert Clarinet "$d/clarinet.sfz" "CC0 1.0 (VSCO 2 Community Edition, Clarinet sustained, https://github.com/sgossner/VSCO-2-CE)" \
    "$opts" "$opts --single-layer" "$opts --single-layer"
}

guitar() {
  local d="$WORK/guitar"
  mkdir -p "$d"
  if [ ! -d "$d/extract" ]; then
    curl -sSfL -o "$d/g.7z" "https://github.com/freepats/electric-guitar-FSBS-direct/releases/download/2022-09-11/EGuitarFSBS-direct-SFZ+FLAC-20220911.7z"
    7z x -y -o"$d/extract" "$d/g.7z" > /dev/null
  fi
  local opts="--mode keyboard --bass 2.0 --treble 0.9 --normalize-layers --tune-correct"
  convert ElectricGuitar "$d/extract/EGuitarFSBS-direct SFZ+FLAC-20220911/EGuitarFSBS-direct bridge 20220911.sfz" \
    "CC0 1.0 (FreePats Electric Guitar FSBS direct, bridge pickup, http://freepats.zenvoid.org)" \
    "$opts" "$opts --single-layer" "$opts --single-layer"
}

bass() {
  local d="$WORK/bass" base=https://raw.githubusercontent.com/sfzinstruments/karoryfer.growlybass/HEAD
  mkdir -p "$d/sustain"
  [ -s "$d/growlybass_clean.sfz" ] || curl -sSf -o "$d/growlybass_clean.sfz" "$base/growlybass_clean.sfz"
  # the first round robin of the soft and loud layer of the sustained notes
  for n in db2 e2 gb2 a2 c3 eb3 gb3 a3 c4 eb4 gb4 a4 c5 eb5; do
    for l in p f; do
      [ -s "$d/sustain/${n}_${l}_rr1.wav" ] || curl -sSf -o "$d/sustain/${n}_${l}_rr1.wav" "$base/sustain/${n}_${l}_rr1.wav"
    done
  done
  # the sfz maps the bass one octave too high
  local opts="--mode keyboard --keys 30-80 --transpose -12 --bass 2.2 --treble 1.2 --normalize-layers --tune-correct --pitch-marks"
  convert ElectricBass "$d/growlybass_clean.sfz" \
    "CC0 1.0 (Karoryfer Growlybass, Squier Jazz Bass, https://github.com/sfzinstruments/karoryfer.growlybass)" \
    "$opts --layer-index 1,2" "$opts --layer-index 2" "$opts --layer-index 2"
}

upright_bass() {
  local d="$WORK/upright-bass" base=https://raw.githubusercontent.com/sfzinstruments/karoryfer.meatbass/main
  mkdir -p "$d/Programs" "$d/Samples/pizz"
  curl -sfI "$base/LICENSE" > /dev/null || base=https://raw.githubusercontent.com/sfzinstruments/karoryfer.meatbass/master
  for f in 04_pizz.sfz pizz_basic.sfz pizz_basic_map.sfz; do
    [ -s "$d/Programs/$f" ] || curl -sSfL -o "$d/Programs/$f" "$base/Programs/$f"
  done
  for f in $(cat "$d/Programs/pizz_basic.sfz" "$d/Programs/pizz_basic_map.sfz" | grep -o "[a-z0-9]*_vl[0-9]_rr1" | sort -u); do
    [ -s "$d/Samples/pizz/$f.wav" ] || curl -sSfL -o "$d/Samples/pizz/$f.wav" "$base/Samples/pizz/$f.wav"
  done
  # c4: the recording of the loudest layer has a wrong pitch (about 6
  # semitones off), the layer below is used instead
  sed 's/c4_vl4_rr1/c4_vl3_rr1/' "$d/Programs/pizz_basic.sfz" > "$d/Programs/pizz_tinyinstruments.sfz"
  local opts="--keys 12-71 --pitch-marks --tune-correct"
  convert UprightBass "$d/Programs/pizz_tinyinstruments.sfz" \
    "CC0 1.0 (Karoryfer Meatbass, 1958 Otto Rubner double bass, pizzicato, https://github.com/sfzinstruments/karoryfer.meatbass)" \
    "$opts --max-layers 2" "$opts --single-layer" "$opts --single-layer"
}

instruments=("$@")
[ ${#instruments[@]} -gt 0 ] || instruments=(upright-piano grand-piano epiano drums sax clarinet guitar bass upright-bass)
for i in "${instruments[@]}"; do
  case $i in
    upright-piano) upright_piano ;;
    grand-piano) grand_piano ;;
    epiano) epiano ;;
    drums) drums ;;
    sax) sax ;;
    clarinet) clarinet ;;
    guitar) guitar ;;
    bass) bass ;;
    upright-bass) upright_bass ;;
    *) echo "unknown instrument: $i" >&2; exit 1 ;;
  esac
done
