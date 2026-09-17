#!/usr/bin/env bash
# tests/vita/boot.sh : boot the built game in the Vita3K emulator and print what it logged.
#
# This is the first thing to run after a build. It does not play the game -- nothing here presses a
# button, and there is no ROM on the emulator's card unless you put one there -- but it answers the
# questions that come before that, and that nothing else can answer: does the module load, does the
# platform layer come up, does the renderer find a GPU, and does the port get far enough to write its
# log and say what it found.
#
# The log is the output. Everything the port reports after boot goes to ux0:data/vitapoke/log.txt on
# the emulator's virtual card, and that is what this prints.
#
#   tests/vita/boot.sh                     boot with whatever is on the card
#   tests/vita/boot.sh --rom FILE          copy FILE in as the ROM first
#   tests/vita/boot.sh --shacccg FILE      copy in libshacccg.suprx, which the renderer needs
#   tests/vita/boot.sh --seconds 60        wait longer than the default 45
#
# Without libshacccg.suprx the renderer stops at startup and says so: vitaGL compiles its shaders on
# the console and there is no compiler in a stock emulator. Everything on the DS side of the renderer
# can still be checked without one -- build it with NO_GPU=1 and FRAME_DUMP=<n> (see
# port/native-vita-render/gpu.cpp) and the composed screens land on the card for
# tests/vita/raw_to_png.py.
#
# Vita3K is not a console: its timing, its scheduling and its GPU are approximations, and its touch
# panel is a mouse. A clean boot here is not proof that a Vita boots it. A crash here is still worth
# reading.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
source "$ROOT/scripts/common.sh"
source "$ROOT/tests/vita/emulator.sh"

TITLE_ID=VPOK00001
APP="$ROOT/.work/vita/test_out/native-audio-app"
SECONDS_TO_WAIT=45
ROM=""
SHACCCG=""
while [ $# -gt 0 ]; do case "$1" in
  --rom) ROM="$2"; shift 2;;
  --shacccg) SHACCCG="$2"; shift 2;;
  --seconds) SECONDS_TO_WAIT="$2"; shift 2;;
  *) die "usage: tests/vita/boot.sh [--rom FILE] [--shacccg libshacccg.suprx] [--seconds N]";;
esac; done

[ -f "$APP/eboot.bin" ] || die "no build to boot. Run: ./build.sh game"

LOGS="$WORK"
vita3k_prepare
vita3k_install "$TITLE_ID" "$APP/eboot.bin" "$APP/param.sfo"

DATA="$PREF/ux0/data/vitapoke"
mkdir -p "$DATA"
RESULT="$DATA/log.txt"
rm -f "$RESULT"
if [ -n "$ROM" ]; then
  [ -f "$ROM" ] || die "ROM not found: $ROM"
  log "Copying $(basename "$ROM") onto the emulator's card"
  cp -f "$ROM" "$DATA/Platinum.nds"
  # The port will not create a save and will not touch one that is the wrong size.
  [ -f "$DATA/Platinum.sav" ] || python3 "$ROOT/scripts/make_save.py" "$DATA/Platinum.sav"
fi

if [ -n "$SHACCCG" ]; then
  [ -f "$SHACCCG" ] || die "not found: $SHACCCG"
  mkdir -p "$PREF/ur0/data"
  cp -f "$SHACCCG" "$PREF/ur0/data/libshacccg.suprx"
  log "Installed the shader compiler into the emulator"
fi

log "Booting vitapoke in Vita3K (up to ${SECONDS_TO_WAIT}s)"
emu_run "$SECONDS_TO_WAIT" -l 2 -r "$TITLE_ID" > "$WORK/boot.log" 2>&1 || true
pkill -9 -x Vita3K 2>/dev/null || true

echo
if [ -f "$RESULT" ]; then
  log "ux0:data/vitapoke/log.txt"
  cat "$RESULT"
else
  log "the port wrote no log at all"
fi
echo
# The emulator's own log says what it thinks happened -- a module that would not load, a missing
# import, an unimplemented call -- which is the half of the story the port cannot tell.
log "Vita3K said (filtered):"
grep -aiE 'error|unimplemen|not found|missing|fatal|abort|exception|failed|module' "$WORK/boot.log" |
  grep -avE 'qt\.|pipewire|PulseAudio' | tail -30 || true
