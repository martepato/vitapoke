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
#   VITAPOKE_VITA3K=/path/to/Vita3K   use an emulator you already have
#   VITAPOKE_V3K_LIBS=/path:/path     extra library directories for the emulator
#
# Needs a display or Xvfb, and an OpenGL driver: llvmpipe is fine, since nothing here draws. The
# downloaded AppImage does not bundle libOpenGL/libEGL -- it expects them from the system, with the GL
# driver -- so on a machine without them, install libopengl0 and libegl1 (Debian/Ubuntu names) or point
# VITAPOKE_V3K_LIBS at a directory holding them.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
source "$ROOT/scripts/common.sh"
export PATH="$VITASDK/bin:$PATH"

TITLE_ID=VPOK00003
WORK="$ROOT/.work/vita3k"
V3K="$CACHE/vita3k"

[ -x "$VITASDK/bin/arm-vita-eabi-gcc" ] || die "VitaSDK not installed. Run: ./build-vita.sh setup"

# ---------------------------------------------------------------- the emulator
#
# Downloading and building happen as whoever invoked this, so the proxy and toolchain configuration of
# that account apply. Only the emulator itself runs as somebody else, and only when necessary: Vita3K
# refuses to run as root, because it would leave root-owned files in the user's own directories.
RUNAS=""
if [ "$(id -u)" = 0 ]; then
  RUNAS=$(getent passwd | awk -F: '$3>=1000 && $3<65534 {print $1; exit}')
  [ -n "$RUNAS" ] || die "Vita3K will not run as root and this system has no unprivileged account to drop to. Run this as a normal user."
  command -v setpriv >/dev/null || die "Vita3K will not run as root and setpriv is not available to drop privileges. Run this as a normal user."
fi

# The emulator keeps its configuration and virtual drive under HOME; give it one inside .work so a
# run never touches the invoking account's real Vita3K installation.
export HOME="$WORK/home"
mkdir -p "$WORK" "$HOME"

if [ -n "${VITAPOKE_VITA3K:-}" ]; then
  EMU="$VITAPOKE_VITA3K"
  [ -x "$EMU" ] || die "VITAPOKE_VITA3K is not executable: $EMU"
else
  EMU="$V3K/squashfs-root/usr/bin/Vita3K"
  if [ ! -x "$EMU" ]; then
    log "Downloading Vita3K (about 65 MB, one time)"
    mkdir -p "$V3K"
    need curl "Install curl."
    curl -fL --progress-bar -o "$V3K/Vita3K.AppImage" \
      "https://github.com/Vita3K/Vita3K/releases/download/continuous/Vita3K-x86_64.AppImage" ||
      die "could not download Vita3K"
    chmod +x "$V3K/Vita3K.AppImage"
    ( cd "$V3K" && ./Vita3K.AppImage --appimage-extract >/dev/null ) ||
      die "could not extract the Vita3K AppImage"
    # Vita3K looks for its builtin shaders and data beside the binary; the AppImage puts them
    # elsewhere, so the zip build's copies are fetched alongside.
    curl -fL -s -o "$V3K/ubuntu-latest.zip" \
      "https://github.com/Vita3K/Vita3K/releases/download/continuous/ubuntu-latest.zip" ||
      die "could not download the Vita3K data files"
    ( cd "$V3K" && unzip -o -q ubuntu-latest.zip shaders-builtin/* 'data/*' &&
      cp -r shaders-builtin data "$V3K/squashfs-root/usr/bin/" )
  fi
fi

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
# The VPK is unpacked straight into the emulator's virtual drive rather than handed to Vita3K to
# install, because the install path goes through a dialog.
PREF="$HOME/.local/share/Vita3K/Vita3K"
APP="$PREF/ux0/app/$TITLE_ID"
RESULT="$PREF/ux0/data/vitapoke-vita3k.txt"
rm -rf "$APP"; mkdir -p "$APP/sce_sys" "$PREF/ux0/data"
cp "$BUILD/eboot.bin" "$APP/eboot.bin"
cp "$BUILD/param.sfo" "$APP/sce_sys/param.sfo"
rm -f "$RESULT"

# Vita3K's own defaults stop an unattended run: a welcome dialog, a missing-firmware warning and an
# update check all wait for a click, and the default renderer is Vulkan, which a software driver does
# not provide. Nothing here draws, so OpenGL on llvmpipe is enough.
#
# The config has to be one Vita3K wrote: it rejects a file that is missing keys it has no default for
# (the keyboard bindings, for one), so a handwritten minimal file will not load. The first run below
# exists only to produce it -- with no title to run, so it cannot fail on an app that is not installed
# yet -- and is expected to sit in its window until the timeout.
CONFIG="$HOME/.config/Vita3K/config.yml"
EMU_LIBS="$V3K/squashfs-root/usr/lib${VITAPOKE_V3K_LIBS:+:$VITAPOKE_V3K_LIBS}"
# Name what is missing rather than letting the loader fail mid-run with a bare message.
MISSING=$(LD_LIBRARY_PATH="$EMU_LIBS" ldd "$EMU" 2>/dev/null | awk '/not found/{print $1}' | sort -u | tr '\n' ' ')
[ -z "$MISSING" ] || die "the emulator is missing shared libraries: $MISSING
Install them (on Debian/Ubuntu libOpenGL.so.0 is in libopengl0 and libEGL.so.1 is in libegl1), or set
VITAPOKE_V3K_LIBS to a directory that has them."

# emu_run SECONDS [ARG...] : run the emulator with the given arguments for at most SECONDS, as the
# unprivileged account when there is one.
#
# The emulator does not quit on SIGTERM, so `timeout` alone waits for it forever; -k follows up with
# SIGKILL. Everything else here is the emulator being a desktop application: it needs a display, and it
# returns to its own window when a title exits rather than terminating.
emu_run() {
  local seconds=$1; shift
  local cmd=(timeout -k 10 "$seconds" "$EMU" "$@")
  if [ -z "${DISPLAY:-}" ]; then
    cmd=(xvfb-run -a -s "-screen 0 960x544x24" "${cmd[@]}")
  fi
  if [ -n "$RUNAS" ]; then
    chown -R "$RUNAS" "$WORK" "$V3K"
    cmd=(setpriv --reuid="$RUNAS" --regid="$RUNAS" --init-groups
         env "HOME=$HOME" "SDL_AUDIODRIVER=dummy" "LD_LIBRARY_PATH=$EMU_LIBS" "${cmd[@]}")
  fi
  ( cd "$(dirname "$EMU")" && SDL_AUDIODRIVER=dummy LD_LIBRARY_PATH="$EMU_LIBS" "${cmd[@]}" ) || true
}

if [ -z "${DISPLAY:-}" ]; then
  command -v xvfb-run >/dev/null || die "no DISPLAY and no xvfb-run: Vita3K needs one or the other."
fi

if [ ! -f "$CONFIG" ]; then
  log "Letting Vita3K write its default configuration"
  emu_run 45 -l 4 > "$WORK/config-run.log" 2>&1
fi
[ -f "$CONFIG" ] || {
  grep -vE 'qt\.|pipewire|PulseAudio' "$WORK/config-run.log" 2>/dev/null | tail -15 >&2
  die "Vita3K did not write a configuration at $CONFIG"
}
sed -i -e 's/^show-welcome: true/show-welcome: false/' \
       -e 's/^warn-missing-firmware: true/warn-missing-firmware: false/' \
       -e 's/^check-for-updates-mode: 1/check-for-updates-mode: 0/' \
       -e 's/^backend-renderer: Vulkan/backend-renderer: OpenGL/' \
       -e 's/^discord-rich-presence: true/discord-rich-presence: false/' \
       -e "s|^pref-path: .*|pref-path: $PREF|" "$CONFIG"

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
