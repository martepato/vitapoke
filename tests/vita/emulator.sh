#!/usr/bin/env bash
# tests/vita/emulator.sh : bring up the Vita3K emulator for an unattended run. Sourced, not run.
#
# Vita3K is a desktop application that expects a person in front of it, and four things it does by
# default stop a script using it. Each is dealt with here, and the reasons are worth keeping written
# down because none of them is obvious from a failure:
#
#   - it refuses to run as root, so that it cannot leave root-owned files in a user's directories;
#   - its generated configuration opens three dialogs that wait for a click, and defaults to a
#     Vulkan renderer that a software driver does not provide;
#   - the downloaded AppImage does not bundle libOpenGL/libEGL, expecting them from the system;
#   - it ignores SIGTERM, so a plain `timeout` waits for it forever.
#
# What a caller gets from sourcing this: $EMU (the emulator), $PREF (its virtual drive), and
# emu_run SECONDS [ARG...]. Callers that redirect its output name that file in EMU_LOG, so the log
# guard below keeps it from growing without bound.
#
# Needs a display or Xvfb, and an OpenGL driver: llvmpipe is fine for anything that does not draw.
#   VITAPOKE_VITA3K=/path/to/Vita3K   use an emulator you already have
#   VITAPOKE_V3K_LIBS=/path:/path     extra library directories for the emulator
[ -n "${ROOT:-}" ] || { echo "tests/vita/emulator.sh must be sourced after scripts/common.sh" >&2; exit 1; }

WORK="$ROOT/.work/vita3k"
V3K="$CACHE/vita3k"

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


PREF="$HOME/.local/share/Vita3K/Vita3K"
CONFIG="$HOME/.config/Vita3K/config.yml"
EMU_LIBS="$V3K/squashfs-root/usr/lib${VITAPOKE_V3K_LIBS:+:$VITAPOKE_V3K_LIBS}"
# Name what is missing rather than letting the loader fail mid-run with a bare message.
MISSING=$(LD_LIBRARY_PATH="$EMU_LIBS" ldd "$EMU" 2>/dev/null | awk '/not found/{print $1}' | sort -u | tr '\n' ' ')
[ -z "$MISSING" ] || die "the emulator is missing shared libraries: $MISSING
Install them (on Debian/Ubuntu libOpenGL.so.0 is in libopengl0 and libEGL.so.1 is in libegl1), or set
VITAPOKE_V3K_LIBS to a directory that has them."

# Vita3K's own log, which it writes whatever we do with its standard output.
EMU_OWN_LOG="$HOME/.cache/Vita3K/vita3k.log"
# How much log either file may hold. The emulator logs every call it finds interesting, which at the
# levels worth running is several gigabytes a minute -- a five-minute boot filled a 250 GB disk and
# the build failed on the next write. Nothing reads more than the tail of these files, so the guard
# below throws the beginning away rather than letting a run take the machine down.
EMU_LOG_CAP=${VITAPOKE_EMU_LOG_CAP:-$((64 * 1024 * 1024))}

# emu_log_guard FILE... : keep each FILE under the cap while the emulator runs, and echo the guard's
# pid. Truncating a file its writer still holds open leaves a hole rather than moving the write
# offset back, so the emulator keeps appending where it was and the disk gets the blocks back.
emu_log_guard() {
  local files=("$@")
  # The guard's own output goes nowhere on purpose: it is started from a command substitution, which
  # reads until every writer of the pipe has closed it, and this one never exits on its own.
  (
    while :; do
      sleep 5
      local f size
      for f in "${files[@]}"; do
        [ -f "$f" ] || continue
        size=$(stat -c %s "$f" 2>/dev/null || echo 0)
        if [ "$size" -gt "$EMU_LOG_CAP" ]; then : > "$f"; fi
      done
    done
  ) >/dev/null 2>&1 & echo $!
}

# emu_run SECONDS [ARG...] : run the emulator with the given arguments for at most SECONDS, as the
# unprivileged account when there is one. Set EMU_LOG to the file the caller redirects into, so the
# guard above covers it too.
#
# The emulator does not quit on SIGTERM, so `timeout` alone waits for it forever; -k follows up with
# SIGKILL. Everything else here is the emulator being a desktop application: it needs a display, and it
# returns to its own window when a title exits rather than terminating.
emu_run() {
  local seconds=$1; shift
  local cmd=(timeout -k 10 "$seconds" "$EMU" "$@")
  local guard; guard=$(emu_log_guard "$EMU_OWN_LOG" ${EMU_LOG:+"$EMU_LOG"})
  if [ -z "${DISPLAY:-}" ]; then
    cmd=(xvfb-run -a -s "-screen 0 960x544x24" "${cmd[@]}")
  fi
  if [ -n "$RUNAS" ]; then
    chown -R "$RUNAS" "$WORK" "$V3K"
    cmd=(setpriv --reuid="$RUNAS" --regid="$RUNAS" --init-groups
         env "HOME=$HOME" "SDL_AUDIODRIVER=dummy" "LD_LIBRARY_PATH=$EMU_LIBS" "${cmd[@]}")
  fi
  ( cd "$(dirname "$EMU")" && SDL_AUDIODRIVER=dummy LD_LIBRARY_PATH="$EMU_LIBS" "${cmd[@]}" ) || true
  kill "$guard" 2>/dev/null || true
}

if [ -z "${DISPLAY:-}" ]; then
  command -v xvfb-run >/dev/null || die "no DISPLAY and no xvfb-run: Vita3K needs one or the other."
fi

# vita3k_prepare : make sure the emulator has a configuration it will start from without a click.
#
# A welcome dialog, a missing-firmware warning and an update check all wait for one, and the default
# renderer is Vulkan, which a software driver does not provide. The configuration has to be one
# Vita3K wrote: it rejects a file missing keys it has no default for (the keyboard bindings, for
# one), so a handwritten minimal file will not load. The first run below exists only to produce it --
# with no title to run, so it cannot fail on an application that is not installed yet -- and is
# expected to sit in its window until the timeout.
vita3k_prepare() {
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
}

# vita3k_install TITLE_ID EBOOT SFO [ASSET_DIR] : put an application on the emulator's virtual drive.
# The VPK is unpacked straight in rather than handed to Vita3K to install, because its install path
# is a dialog. ASSET_DIR, when given, is copied in beside the executable, which is where a VPK built
# with a ROM puts the game's data and where the port looks for it as app0:.
vita3k_install() {
  local id=$1 eboot=$2 sfo=$3 assets=${4:-} app="$PREF/ux0/app/$1"
  rm -rf "$app"; mkdir -p "$app/sce_sys" "$PREF/ux0/data"
  cp "$eboot" "$app/eboot.bin"
  cp "$sfo" "$app/sce_sys/param.sfo"
  if [ -n "$assets" ] && [ -d "$assets" ]; then
    # Excluding the build's own stamp, which is not part of the application.
    rsync -a --exclude .stamp "$assets/" "$app/" ||
      die "could not copy the unpacked game data into the emulator"
    log "Installed $(find "$assets" -type f ! -name .stamp | wc -l) unpacked game files"
  fi
}
