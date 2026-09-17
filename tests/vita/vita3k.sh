#!/usr/bin/env bash
# tests/vita/vita3k.sh : run the platform layer's runtime checks on Vita3K.
#
# tests/vita/run.sh proves port/vita compiles and links. This runs it: it builds tests/vita/ds_runtime.c
# into a VPK, boots it in the Vita3K emulator, and reads back the report the test writes to
# ux0:data/vitapoke-vita3k.txt. It is the only thing short of a console that checks what the platform
# layer *does* -- that the DS execution lock really serialises DS threads, that a wake is not lost, that
# alarms fire and cancel, and that the psp2 semantics the design rests on are what the port assumes.
#
# It is separate from run.sh because it downloads an emulator (about 65 MB) and takes a few minutes.
# Vita3K has no tagged releases -- everything is the rolling `continuous` build -- so unlike the
# toolchain this cannot be pinned, and the version actually used is printed instead.
#
#   tests/vita/vita3k.sh              download Vita3K if needed, build, run, report
#
# Bringing the emulator up unattended is tests/vita/emulator.sh, which this shares with
# tests/vita/boot.sh; its header has the requirements and the environment variables.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
source "$ROOT/scripts/common.sh"
source "$ROOT/tests/vita/emulator.sh"
export PATH="$VITASDK/bin:$PATH"

TITLE_ID=VPOK00003

[ -x "$VITASDK/bin/arm-vita-eabi-gcc" ] || die "VitaSDK not installed. Run: ./build.sh setup"

# ---------------------------------------------------------------- build the test
log "Building the runtime test"
LOGS="$WORK"
BUILD="$WORK/build"
rm -rf "$BUILD"; mkdir -p "$BUILD"

U="$CACHE/upstream"
"$ROOT/scripts/fetch.sh" libntr
GEN="$ROOT/.work/vita-gen"
mkdir -p "$GEN/nitro/fx"
python3 "$U/libntr/gen/nitro/fx/gen_fx_const.py" \
  "$U/libntr/gen/nitro/fx/fx_const.csv" "$GEN/nitro/fx/fx_const.h"

NITRO=(-DPM_KEEP_ASSERTS -DSDK_PORT -DSDK_X86 -DSDK_BUILD_VITA -DSDK_VERSION_MAJOR=4 -DSDK_TS
       -DSDK_4M -DSDK_FINALROM -DNNS_FINALROM)
INC=(-I"$U/libntr/include" -I"$GEN" -I"$ROOT/port/vita/sdl2-shim" -I"$ROOT/port/vita/include")
PLATFORM=("$ROOT/port/vita/os_core.c" "$ROOT/port/vita/os_alarm.c" "$ROOT/port/vita/os_thread.c"
          "$ROOT/port/vita/input.c" "$ROOT/port/vita/sdl_sync.c")

( cd "$BUILD"
  arm-vita-eabi-gcc -O2 -std=gnu99 -mtune=cortex-a9 -mfpu=neon -Wl,-q "${NITRO[@]}" "${INC[@]}" \
    -o test.elf "$ROOT/tests/vita/ds_runtime.c" "${PLATFORM[@]}" \
    -lSceCtrl_stub -lSceTouch_stub -lSceRtc_stub -lSceLibKernel_stub 2>&1 | grep -E 'error:' && exit 1
  vita-elf-create test.elf test.velf >/dev/null
  vita-make-fself -q test.velf eboot.bin
  vita-mksfoex -s "TITLE_ID=$TITLE_ID" "vitapoke runtime checks" param.sfo >/dev/null
) || die "could not build the runtime test"

# ---------------------------------------------------------------- install and run
#
# Everything the emulator needs to be persuaded of is in tests/vita/emulator.sh.
vita3k_prepare
vita3k_install "$TITLE_ID" "$BUILD/eboot.bin" "$BUILD/param.sfo"
RESULT="$PREF/ux0/data/vitapoke-vita3k.txt"
rm -f "$RESULT"

log "Running the checks in Vita3K"
# The title writes its report and exits, but the emulator then goes back to its window, so waiting for
# the emulator to finish would mean waiting for the timeout every time. Wait for the report instead and
# stop the emulator as soon as it lands; the timeout is only the backstop for a title that never
# finishes.
emu_run 240 -l 3 -r "$TITLE_ID" > "$WORK/emulator.log" 2>&1 &
EMU_JOB=$!
for _ in $(seq 1 240); do
  [ -f "$RESULT" ] && break
  kill -0 "$EMU_JOB" 2>/dev/null || break
  sleep 1
done
# Give the title a moment to close the file, then stop the emulator.
sleep 2
pkill -9 -x Vita3K 2>/dev/null || true
wait "$EMU_JOB" 2>/dev/null || true

[ -f "$RESULT" ] || {
  echo "--- emulator log (tail) ---" >&2
  grep -vE 'qt\.|pipewire|PulseAudio' "$WORK/emulator.log" | tail -25 >&2
  die "the test did not produce a report; it may have crashed or never booted (see $WORK/emulator.log)"
}

echo
cat "$RESULT"
echo

if grep -q '^ALL PASSED' "$RESULT"; then
  log "Vita3K checks passed"
else
  echo "--- emulator log (tail) ---" >&2
  grep -vE 'qt\.|pipewire|PulseAudio' "$WORK/emulator.log" | tail -25 >&2
  die "Vita3K checks failed"
fi
