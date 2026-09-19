#!/usr/bin/env bash
# tests/vita/town.sh : drive the built game into the overworld, stand still, and report the frame
# profile. The loop this port is tuned in.
#
# Measuring a change to the renderer or the geometry simulator used to mean asking whoever owns a
# console to build, run, walk into town and send back a log -- once per idea. This does the same
# thing unattended: it boots the emulator, works the buttons until the log says the field map is
# running, stops touching the controls so the character stays put, and then collects whole [PERF]
# windows from a scene that is not moving.
#
#   tests/vita/town.sh                      run with whatever save is already installed
#   tests/vita/town.sh --save FILE          install a 512 KiB save first (yours; see below)
#   tests/vita/town.sh --build              build before running
#   tests/vita/town.sh --windows 3          collect three report windows rather than two
#   tests/vita/town.sh --throttle 35        hold the emulator to 35% of a core (see --throttle)
#   tests/vita/town.sh --shot town.png      write the screen when the measurement ends
#   tests/vita/town.sh --keep               leave the emulator running afterwards
#
# THE SAVE IS YOURS AND STAYS OUT OF THE REPOSITORY. --save copies it to .work/vita3k-save/, which
# is ignored, and from there onto the emulator's card. It is a save from the game, so it is game
# data: do not add it to git, and note that *.sav is ignored anyway.
#
# Getting to town is done by watching the log rather than by timing key presses, because the timing
# is not reproducible -- an emulator that is slow today reaches the title screen a minute later than
# one that is fast. The script mashes START and CIRCLE until "enter gFieldMapTemplate" appears, which
# is the game itself saying the overworld is up, and then stops.
#
# WHAT THE NUMBERS MEAN. Vita3K is not a Vita: its CPU is a recompiler on a desktop processor, so
# absolute microseconds here are not the console's. What carries across is the shape -- which bucket
# grew, how many vertices a scene sends, whether a change moved game_us at all -- and the counters
# that are properties of the game rather than of the machine (verts, normals, polygons, binds).
# Treat a result here as a direction to confirm on hardware, never as the hardware answer.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
source "$ROOT/scripts/common.sh"
source "$ROOT/tests/vita/emulator.sh"

TITLE_ID=VPOK00001
APP="$ROOT/.work/vita/test_out/native-audio-app"
SAVE=""
BUILD=0
WINDOWS=2
THROTTLE=""
SHOT=""
KEEP=0
BUDGET=1500        # seconds before giving up on the whole run
while [ $# -gt 0 ]; do case "$1" in
  --save) SAVE="$2"; shift 2;;
  --build) BUILD=1; shift;;
  --windows) WINDOWS="$2"; shift 2;;
  --throttle) THROTTLE="$2"; shift 2;;
  --shot) SHOT="$2"; shift 2;;
  --keep) KEEP=1; shift;;
  --budget) BUDGET="$2"; shift 2;;
  *) die "usage: tests/vita/town.sh [--save FILE] [--build] [--windows N] [--throttle PERCENT]
                          [--shot FILE.png] [--keep] [--budget SECONDS]";;
esac; done

if [ "$BUILD" = 1 ]; then "$ROOT/build.sh" game; fi
[ -f "$APP/eboot.bin" ] || die "no build to run. Run: ./build.sh game"

for tool in xdotool Xvfb; do
  need "$tool" "Install $tool (Debian/Ubuntu: apt-get install xdotool xvfb)."
done

# The save lives outside the repository on purpose; see the note at the top.
SAVE_KEEP="$ROOT/.work/vita3k-save/Platinum.sav"
if [ -n "$SAVE" ]; then
  [ -f "$SAVE" ] || die "no such save: $SAVE"
  size=$(stat -c %s "$SAVE")
  [ "$size" = 524288 ] || die "a Platinum save is 524288 bytes; $SAVE is $size"
  mkdir -p "$(dirname "$SAVE_KEEP")"
  cp -f "$SAVE" "$SAVE_KEEP"
  log "Keeping your save in .work/vita3k-save/ (ignored by git)"
fi

LOGS="$WORK"
vita3k_prepare
ASSETS="$ROOT/.work/vita/assets"
[ -f "$ASSETS/nitrofs.idx" ] || ASSETS=""
vita3k_install "$TITLE_ID" "$APP/eboot.bin" "$APP/param.sfo" "$ASSETS"

DATA="$PREF/ux0/data/vitapoke"
mkdir -p "$DATA"
GAMELOG="$DATA/log.txt"
rm -f "$GAMELOG"
if [ -f "$SAVE_KEEP" ]; then
  cp -f "$SAVE_KEEP" "$DATA/Platinum.sav"
  log "Installed the save onto the emulator's card"
else
  log "No save installed: the run will reach the title screen and stop there"
fi
find "$PREF/ux0/data/shader_cache" -type f -size 0 -delete 2>/dev/null || true

# A display this script can reach, so it can press keys and take the screenshot.
pkill -9 -x Vita3K 2>/dev/null || true
XVFB_PID=""
if [ -z "${DISPLAY:-}" ]; then
  for n in 99 98 97 96 95; do
    if ! [ -e "/tmp/.X11-unix/X$n" ]; then
      Xvfb ":$n" -ac -screen 0 960x544x24 > "$WORK/xvfb.log" 2>&1 &
      XVFB_PID=$!
      export DISPLAY=":$n"
      break
    fi
  done
  [ -n "$XVFB_PID" ] || die "could not find a free X display"
fi

cleanup() {
  [ "$KEEP" = 1 ] || pkill -9 -x Vita3K 2>/dev/null || true
  if [ -n "$XVFB_PID" ]; then kill "$XVFB_PID" 2>/dev/null || true; fi
  return 0
}
trap cleanup EXIT

log "Booting"
EMU_LOG="$WORK/town-emu.log" emu_run "$BUDGET" -l 3 -r "$TITLE_ID" > "$WORK/town-emu.log" 2>&1 &
EMU_WAIT=$!

# --throttle PERCENT : hold the emulator to that share of one core, so that a scene which the host
# walks through has to work for its frames the way the console does. It is a share of CPU time, not
# a slower processor -- the mix of work is unchanged and only the rate is -- so it makes "does this
# hold 30" answerable without pretending to be a Vita. Calibrate it with docs/PERF-BASELINE.md.
throttle_on() {
  local pct=$1 period=100000 quota cg
  quota=$((period * pct / 100))
  # cgroup v2 first, then v1: this container has v1, a console runner may have either.
  if [ -w /sys/fs/cgroup/cgroup.subtree_control ]; then
    cg=/sys/fs/cgroup/vitapoke
    mkdir -p "$cg" 2>/dev/null || true
    echo "+cpu" > /sys/fs/cgroup/cgroup.subtree_control 2>/dev/null || true
    echo "$quota $period" > "$cg/cpu.max" 2>/dev/null || { log "no cgroup v2 cpu control"; return 1; }
  elif [ -d /sys/fs/cgroup/cpu ]; then
    cg=/sys/fs/cgroup/cpu/vitapoke
    mkdir -p "$cg" 2>/dev/null || true
    echo "$period" > "$cg/cpu.cfs_period_us" 2>/dev/null || { log "no cgroup v1 cpu control"; return 1; }
    echo "$quota"  > "$cg/cpu.cfs_quota_us"  2>/dev/null || { log "no cgroup v1 cpu control"; return 1; }
  else
    log "no cgroup cpu controller: running at host speed"
    return 1
  fi
  # Every thread of the emulator, not just its main one: the quota is shared between them, which is
  # the point -- a Vita's cores are not free either.
  local moved=0 p t
  for p in $(pgrep -x Vita3K 2>/dev/null || true); do
    for t in /proc/"$p"/task/*; do
      if echo "${t##*/}" > "$cg/tasks" 2>/dev/null || echo "${t##*/}" > "$cg/cgroup.procs" 2>/dev/null; then
        moved=$((moved + 1))
      fi
    done
  done
  log "Throttled to ${pct}% of a core across $moved threads"
  return 0
}

# wait_for LABEL PATTERN SECONDS [ACTION] : poll the game's own log for a line, running ACTION each
# time round. The log is the only thing that says where the game actually is.
wait_for() {
  local label=$1 pattern=$2 limit=$3 action=${4:-}
  local waited=0
  while [ "$waited" -lt "$limit" ]; do
    if grep -aq "$pattern" "$GAMELOG" 2>/dev/null; then
      log "$label after ${waited}s"
      return 0
    fi
    kill -0 "$EMU_WAIT" 2>/dev/null || { log "the emulator exited early"; return 1; }
    if [ -n "$action" ]; then $action || true; fi
    sleep 2; waited=$((waited + 2))
  done
  log "gave up waiting for $label after ${limit}s"
  return 1
}

WINDOW=""
find_window() {
  if [ -n "$WINDOW" ]; then return 0; fi
  WINDOW=$(xdotool search --name '^Vita3K$' 2>/dev/null | tail -1 || true)
  [ -n "$WINDOW" ] || return 1
  log "emulator window $WINDOW"
  return 0
}

# Vita3K maps Return to START and c to CIRCLE, which is the DS's A. A tap does not register -- the
# emulator samples the keyboard per frame and a frame here can be 40 ms -- so each is held.
mash() {
  find_window || return 0
  xdotool windowfocus "$WINDOW" 2>/dev/null || true
  for key in Return c; do
    xdotool keydown "$key" 2>/dev/null || true
    sleep 0.4
    xdotool keyup "$key" 2>/dev/null || true
    sleep 0.2
  done
}

wait_for "window and first frame" "entering NitroMain" 180 find_window || exit 1
wait_for "reached the overworld" "enter gFieldMapTemplate" 900 mash || {
  log "--- what the game logged ---"; tail -25 "$GAMELOG" 2>/dev/null || true; exit 1; }

# From here on nothing is pressed, so the character stands where the save put them. The windows
# already in the log were spent getting here; only the ones after this point are the measurement.
perf_lines() { grep -ac '^\[PERF\]' "$GAMELOG" 2>/dev/null || true; }
# The throttle goes on here rather than at boot: getting to town is not the measurement, and a
# quota applied from the start makes every iteration three times longer for nothing.
if [ -n "$THROTTLE" ]; then throttle_on "$THROTTLE" || true; fi

BASE=$(perf_lines); BASE=${BASE:-0}
log "Standing still; collecting $WINDOWS report window(s)"
waited=0
while [ "$waited" -lt "$BUDGET" ]; do
  have=$(perf_lines); have=${have:-0}
  if [ "$((have - BASE))" -ge "$WINDOWS" ]; then
    log "collected $WINDOWS window(s) after ${waited}s"
    break
  fi
  if ! kill -0 "$EMU_WAIT" 2>/dev/null; then
    log "the emulator stopped after ${waited}s with $((have - BASE)) of $WINDOWS window(s)"
    log "--- the emulator's last words ---"
    grep -avE 'Unhandled SIGSEGV|io_error_impl|Stubbed|duplicate messages' "$WORK/town-emu.log" |
      tail -8 || true
    break
  fi
  sleep 5; waited=$((waited + 5))
done

if [ -n "$SHOT" ]; then
  xwd -display "$DISPLAY" -root -silent 2>/dev/null | xwdtopnm 2>/dev/null | pnmtopng > "$SHOT" 2>/dev/null &&
    log "wrote $SHOT" || log "could not capture $SHOT"
fi

echo
log "Standing in the overworld -- the last $WINDOWS window(s)"
for tag in '^\[PERF\]' '^\[GPROF\]' '^\[GPROF-ALLOC\]' '^\[AUDIO\]'; do
  grep -a "$tag" "$GAMELOG" 2>/dev/null | tail -"$WINDOWS" || true
done
echo
grep -aE '^\[TEXTURE\]|^\[G3\]|^\[RENDER\]' "$GAMELOG" 2>/dev/null | head -8 || true
echo
log "Anything that went wrong"
grep -aiE 'fatal|refused|not decoded|no VRAM bank|list full|seek failed' "$GAMELOG" 2>/dev/null | head -10 ||
  echo "    nothing"
echo
log "full log: $GAMELOG"
