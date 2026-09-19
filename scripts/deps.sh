#!/usr/bin/env bash
# deps.sh : build the pinned GPU dependencies of the Vita port into the toolchain.
#
# The renderer puts the DS's screens on the display through vitaGL (OpenGL over GXM); everything it
# asks of the GPU is in port/native-vita-render/gpu.h, and gpu.cpp is the only file in the port that
# includes a GL header. vitaGL needs math-neon, and vitaShaRK, because vitaGL writes the shaders for
# its fixed-function pipeline as source and compiles them on the console: there is no precompiled
# path. vitaShaRK is the wrapper around SceShaccCg that does that, so the renderer needs
# libshacccg.suprx present at runtime -- see docs/VITA.md for what that means and why it is
# acceptable.
#
# Building vitaShaRK needs one header VitaSDK does not ship (shacccg_ext.h, from the official SDK's
# shacccg package); port/vita/shacccg_ext.h is that declaration and port/vita/shacccg_ext_stub.c
# defines the function, because there is no import stub for it either. The renderer never calls it.
#
# Everything is installed into $VITASDK, which is .cache/vitasdk unless you set VITASDK yourself. When
# it is your own install, this lists everything it would write and asks before writing any of it --
# VITAPOKE_ASSUME_YES=1 answers yes, and VITAPOKE_SKIP_DEPS=1 keeps the libraries that are already
# there and installs only the headers the build cannot compile without.
source "$(dirname "$0")/common.sh"
export PATH="$VITASDK/bin:$PATH"
U="$CACHE/upstream"
LOGS="$ROOT/.work/tree/logs"; mkdir -p "$LOGS"   # `step` writes each command's output here
STAMP="$VITASDK/.vitapoke-deps"
SHIMSTAMP="$VITASDK/.vitapoke-shims"

# What this script would install, as one string each, so a run that has nothing to do says so and
# writes nothing -- which matters most when $VITASDK is somebody's own install.
#
# HEADERS covers the two headers and the SDL declarations: libntr's OS headers include <SDL2/SDL.h>
# and <SDL2/SDL_thread.h> on any target that is not SDK_BUILD_ARM, so the whole build needs them on
# the include path and not just port/vita, and VitaSDK ships no SDL -- so the declarations libntr
# actually refers to come from port/vita/sdl2-shim. shacccg_ext.h is there for vitaShaRK; see the note
# at the top. Their contents are hashed rather than assumed unchanged, because an edit to a shim has
# to reach an install whose libraries are already built.
HEADERS="$(cat "$ROOT"/port/vita/sdl2-shim/SDL2/*.h "$ROOT/port/vita/shacccg_ext.h" |
           if command -v sha1sum >/dev/null; then sha1sum; else shasum -a 1; fi | cut -d' ' -f1)"
LIBS="$(awk '$1=="vitaGL"||$1=="math-neon"||$1=="vitaShaRK"{printf "%s=%s ", $1, $3}' "$ROOT/third_party.lock")"
# The vitaGL build flags are in the stamp too: they change the library without changing its revision,
# and an install carrying the old ones would otherwise look up to date.
VGLFLAGS="NO_DEBUG NO_SPLASHSCREEN TEXTURES_SPEEDHACK"
WANT="$LIBS headers=$HEADERS vitagl_flags=$VGLFLAGS"

install_headers() {
  install -d "$VITASDK/arm-vita-eabi/include/SDL2"
  install -m644 "$ROOT"/port/vita/sdl2-shim/SDL2/*.h "$VITASDK/arm-vita-eabi/include/SDL2/"
  install -m644 "$ROOT/port/vita/shacccg_ext.h" "$VITASDK/arm-vita-eabi/include/shacccg_ext.h"
  printf '%s' "$HEADERS" > "$SHIMSTAMP"
}

# confirm WHAT... : when $VITASDK is an install this project did not create, say what is about to be
# written into it and get a yes. Nothing has been written when this is reached.
confirm() {
  [ "$VITAPOKE_OWN_TOOLCHAIN" = 1 ] || return 0
  local line ans
  echo "This writes into your own VitaSDK at $VITASDK:"
  for line in "$@"; do echo "  - $line"; done
  if [ "${VITAPOKE_ASSUME_YES:-0}" = 1 ]; then return 0; fi
  [ -t 0 ] || die "not an interactive terminal: set VITAPOKE_ASSUME_YES=1 to agree to the above, or
unset VITASDK to use the pinned toolchain in .cache/vitasdk, which this project owns."
  printf 'Go ahead? [y/N] '
  read -r ans
  case "$ans" in y|Y|yes|YES) return 0;; esac
  die "nothing was installed. Unset VITASDK to build against the pinned toolchain in .cache/vitasdk instead."
}

# VITAPOKE_SKIP_DEPS=1: keep the libraries already in this install and build none of them.
#
# For somebody who has their own vitaGL, vitaShaRK and math-neon and does not want three of them
# replaced -- see "Using a VitaSDK you already have" in README.md. The headers still have to go in,
# because nothing compiles without them. The library stamp is deliberately not written, so a later run
# without this variable still builds the pinned libraries; the header stamp is, so repeating this is
# silent. Whether the libraries that are already there will do is what ./build.sh check answers: it
# compiles against every vitaGL feature the renderer uses.
if [ "${VITAPOKE_SKIP_DEPS:-0}" = 1 ]; then
  [ "$(cat "$SHIMSTAMP" 2>/dev/null)" = "$HEADERS" ] &&
    { echo "    VITAPOKE_SKIP_DEPS=1: headers already installed, libraries left alone"; exit 0; }
  confirm "the SDL2 declarations libntr refers to, in arm-vita-eabi/include/SDL2" \
          "shacccg_ext.h, in arm-vita-eabi/include" \
          "and nothing else: VITAPOKE_SKIP_DEPS=1 keeps your vitaGL, vitaShaRK and math-neon"
  install_headers
  echo "    VITAPOKE_SKIP_DEPS=1: kept the vitaGL, vitaShaRK and math-neon already in $VITASDK"
  echo "    Installed the headers only. Run ./build.sh check next: it says whether they will do."
  exit 0
fi

[ "$(cat "$STAMP" 2>/dev/null)" = "$WANT" ] && { echo "    GPU dependencies and headers already installed"; exit 0; }

confirm "vitaGL, vitaShaRK and math-neon at the revisions in third_party.lock, replacing any" \
        "  copies already there, in arm-vita-eabi/lib and arm-vita-eabi/include" \
        "the SDL2 declarations libntr refers to, in arm-vita-eabi/include/SDL2" \
        "shacccg_ext.h, in arm-vita-eabi/include"
install_headers

log "Building the Vita GPU dependencies (vitaGL, vitaShaRK, math-neon)"
"$ROOT/scripts/fetch.sh" math-neon vitaShaRK vitaGL

step vita-shark-header bash -c "install -m644 '$U/vitaShaRK/source/vitashark.h' '$VITASDK/arm-vita-eabi/include/vitashark.h' && install -m644 '$ROOT/port/vita/shacccg_ext.h' '$VITASDK/arm-vita-eabi/include/shacccg_ext.h'"
step vita-shark        make -C "$U/vitaShaRK" -j"$(nproc 2>/dev/null || echo 4)" install
step vita-math-neon    make -C "$U/math-neon" -j"$(nproc 2>/dev/null || echo 4)" install
# NO_DEBUG drops vitaGL's error-string paths; NO_SPLASHSCREEN drops the startup logo. Neither belongs in
# a build that boots straight into a game.
#
# TEXTURES_SPEEDHACK turns off vitaGL's automatic texture orphaning, and the name undersells what it
# costs to leave on. Without it, every glTexSubImage2D on a texture drawn in the last four frames
# allocates a fresh buffer the size of the whole texture, copies the old contents into it, frees the
# old one and re-points the GXM texture -- before it copies the pixels the caller asked it to copy.
# The renderer uploads a 256x256 panel every frame and every one of those uploads paid for it: better
# than four milliseconds of a thirty-eight millisecond frame, spent copying a texture to itself.
#
# What the orphaning protects against is writing a texture the GPU has not finished reading. The
# renderer does not need protecting: it cycles each panel through as many textures as vitaGL has
# display buffers, so the one being written is never one the GPU can still be reading. See the note
# where they are created in port/native-vita-render/gpu.cpp. It is a hack only for a program that
# does not do that.
step vita-gl           make -C "$U/vitaGL" -j"$(nproc 2>/dev/null || echo 4)" NO_DEBUG=1 NO_SPLASHSCREEN=1 \
                            TEXTURES_SPEEDHACK=1 install

printf '%s' "$WANT" > "$STAMP"
echo "    vitaGL, vitaShaRK and math-neon installed in $VITASDK"
