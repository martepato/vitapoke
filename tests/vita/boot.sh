#!/usr/bin/env bash
# tests/vita/boot.sh : boot the built game in the Vita3K emulator and print what it logged.
#
# This is the first thing to run after a build. It answers the questions that come before playing,
# and that nothing else can answer: does the module load, does the platform layer come up, does the
# renderer find a GPU, and does the port get far enough to write its log and say what it found. With
# --press it will also work the buttons, which is as close to playing as an unattended run gets.
#
# The log is the output. Everything the port reports after boot goes to ux0:data/vitapoke/log.txt on
# the emulator's virtual card, and that is what this prints.
#
#   tests/vita/boot.sh                     boot with whatever is on the card
#   tests/vita/boot.sh --rom FILE          copy FILE in as the ROM first
#   tests/vita/boot.sh --shacccg FILE      copy in libshacccg.suprx, which the renderer needs
#   tests/vita/boot.sh --seconds 60        wait longer than the default 45
#   tests/vita/boot.sh --press 120:Return,130:x   press keys at those run times, in seconds
#   tests/vita/boot.sh --log-level 2       more from the emulator (0 is everything, 3 the default)
#
# --press needs an X server the script can reach, so it starts its own rather than letting the
# emulator hide one behind xvfb-run: Vita3K takes its buttons from the keyboard (Enter is START, X is
# CROSS, the arrow keys are the d-pad -- the full list is in the emulator's config.yml), and the port
# writes one [INPUT] line for the first press it sees. Each key is pressed for a third of a second,
# which is several frames even at the speed the emulator manages.
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
PRESS=""
# The emulator's log level. 3 is warnings and errors, which is what a boot is read for -- an
# unimplemented import, a module that would not load, a bad memory access. Below that it logs calls,
# several gigabytes a minute of them, which is occasionally what you want and never the default.
LOG_LEVEL=3
while [ $# -gt 0 ]; do case "$1" in
  --rom) ROM="$2"; shift 2;;
  --shacccg) SHACCCG="$2"; shift 2;;
  --seconds) SECONDS_TO_WAIT="$2"; shift 2;;
  --press) PRESS="$2"; shift 2;;
  --log-level) LOG_LEVEL="$2"; shift 2;;
  *) die "usage: tests/vita/boot.sh [--rom FILE] [--shacccg libshacccg.suprx] [--seconds N]
                          [--press SECONDS:KEY[,SECONDS:KEY...]] [--log-level 0-4]";;
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

# press_keys : send the --press schedule to the emulator's window once it has one. Runs alongside the
# emulator, in run time from the moment it was started.
press_keys() {
  local elapsed=0 window="" spec at key
  # The window takes a few seconds to appear, and pressing keys at nothing is not worth reporting.
  while [ -z "$window" ] && [ "$elapsed" -lt 30 ]; do
    sleep 2; elapsed=$((elapsed + 2))
    window=$(xdotool search --name '^Vita3K$' 2>/dev/null | tail -1 || true)
  done
  [ -n "$window" ] || { log "no emulator window appeared: --press did nothing"; return; }
  xdotool windowfocus "$window" 2>/dev/null || true
  for spec in ${PRESS//,/ }; do
    at=${spec%%:*}; key=${spec#*:}
    while [ "$elapsed" -lt "$at" ]; do sleep 1; elapsed=$((elapsed + 1)); done
    # windowfocus again each time: the emulator opens and closes windows of its own as a title starts.
    xdotool windowfocus "$window" 2>/dev/null || true
    xdotool keydown "$key" 2>/dev/null || true
    sleep 0.33
    xdotool keyup "$key" 2>/dev/null || true
    log "pressed $key at ${at}s"
  done
}

# --press needs a display this script can also talk to. xvfb-run, which emu_run falls back to, picks a
# display number inside the call and does not say which, so start one here instead. -ac because the
# emulator runs as a different account than this script (see emulator.sh) and would otherwise be
# refused by X's access control.
XVFB_PID=""
if [ -n "$PRESS" ] && [ -z "${DISPLAY:-}" ]; then
  need xdotool "Install xdotool (Debian/Ubuntu: apt-get install xdotool)."
  need Xvfb "Install Xvfb (Debian/Ubuntu: apt-get install xvfb)."
  for n in 99 98 97 96; do
    if ! [ -e "/tmp/.X11-unix/X$n" ]; then
      Xvfb ":$n" -ac -screen 0 960x544x24 > "$WORK/xvfb.log" 2>&1 &
      XVFB_PID=$!
      export DISPLAY=":$n"
      break
    fi
  done
  [ -n "$XVFB_PID" ] || die "could not find a free X display for --press"
  trap 'kill "$XVFB_PID" 2>/dev/null || true' EXIT
fi

log "Booting vitapoke in Vita3K (up to ${SECONDS_TO_WAIT}s)"
[ -n "$PRESS" ] && press_keys &
PRESS_PID=$!
EMU_LOG="$WORK/boot.log" emu_run "$SECONDS_TO_WAIT" -l "$LOG_LEVEL" -r "$TITLE_ID" > "$WORK/boot.log" 2>&1 || true
kill "$PRESS_PID" 2>/dev/null || true
pkill -9 -x Vita3K 2>/dev/null || true

echo
if [ -f "$RESULT" ]; then
  log "ux0:data/vitapoke/log.txt"
  # A long run's log is mostly the same line repeated, and the interesting parts are at both ends:
  # startup at the top, whatever it was doing when the timeout arrived at the bottom. The whole file
  # is on the card either way, and the path is printed above.
  lines=$(wc -l < "$RESULT")
  if [ "$lines" -le 200 ]; then
    cat "$RESULT"
  else
    head -80 "$RESULT"
    echo "    ... $((lines - 160)) lines ..."
    tail -80 "$RESULT"
  fi
else
  log "the port wrote no log at all"
fi
echo
# The emulator's own log says what it thinks happened -- a module that would not load, a missing
# import, an unimplemented call -- which is the half of the story the port cannot tell.
log "Vita3K said (filtered):"
grep -aiE 'error|unimplemen|not found|missing|fatal|abort|exception|failed|module' "$WORK/boot.log" |
  grep -avE 'qt\.|pipewire|PulseAudio' | tail -30 || true
