#!/usr/bin/env bash
# deps.sh : build the pinned GPU dependencies of the Vita port into the toolchain.
#
# The renderer puts the DS's screens on the display through vitaGL (OpenGL over GXM); everything it
# asks of the GPU is in port/native-vita-render/gpu.h, and gpu.cpp is the only file in the port that
# includes a GL header. vitaGL needs math-neon, and its own header includes vitashark.h, so that
# header is installed too. The vitaShaRK library is not: the runtime GLSL compiler it wraps needs
# SceShaccCg, which only exists on a console if the player extracts libshacccg.suprx from a firmware
# update. port/vita/shark_stub.c answers those calls with "no compiler" instead, and the application
# links SceShaccCg weakly, so it loads on a stock console. The renderer only ever uses vitaGL's
# precompiled fixed-function shaders, which is why it also runs in an emulator -- see docs/VITA.md.
#
# Everything is installed into $VITASDK, which is .cache/vitasdk unless you set VITASDK yourself. When it
# is your own install, this writes into it, so it asks first.
source "$(dirname "$0")/common.sh"
export PATH="$VITASDK/bin:$PATH"
U="$CACHE/upstream"
LOGS="$ROOT/.work/tree/logs"; mkdir -p "$LOGS"   # `step` writes each command's output here
STAMP="$VITASDK/.vitapoke-deps"
WANT="$(awk '$1=="vitaGL"||$1=="math-neon"||$1=="vitaShaRK"{printf "%s=%s ", $1, $3}' "$ROOT/third_party.lock")"

# libntr's OS headers include <SDL2/SDL.h> and <SDL2/SDL_thread.h> on any target that is not
# SDK_BUILD_ARM, so the whole build needs them on the include path -- not just port/vita. VitaSDK ships
# no SDL, so the declarations libntr actually refers to are installed from port/vita/sdl2-shim. Done before the stamp check because it is nearly free and a
# change to the shim has to land even when the GPU libraries are already built.
install -d "$VITASDK/arm-vita-eabi/include/SDL2"
install -m644 "$ROOT"/port/vita/sdl2-shim/SDL2/*.h "$VITASDK/arm-vita-eabi/include/SDL2/"

[ "$(cat "$STAMP" 2>/dev/null)" = "$WANT" ] && { echo "    Vita SDL2 declarations installed; GPU dependencies already built"; exit 0; }

if [ "$VITAPOKE_OWN_TOOLCHAIN" = 1 ]; then
  echo "This installs vitaGL and math-neon into your own VitaSDK at $VITASDK."
  if [ "${VITAPOKE_ASSUME_YES:-0}" = 1 ]; then ans=y
  elif [ -t 0 ]; then printf 'Install them there? [y/N] '; read -r ans
  else die "not an interactive terminal: unset VITASDK to use the pinned toolchain in .cache/vitasdk, or set VITAPOKE_ASSUME_YES=1"; fi
  case "$ans" in y|Y|yes|YES) ;; *) die "unset VITASDK to build against the pinned toolchain in .cache/vitasdk instead";; esac
fi

log "Building the Vita GPU dependencies (vitaGL, math-neon)"
"$ROOT/scripts/fetch.sh" math-neon vitaShaRK vitaGL

# vitashark.h only: vitaGL.h includes it for the shark_* declarations. Building the library needs
# shacccg_ext.h from the SDK's shacccg package, and we do not link it (see the note above).
step vita-shark-header install -m644 "$U/vitaShaRK/source/vitashark.h" "$VITASDK/arm-vita-eabi/include/vitashark.h"
step vita-math-neon    make -C "$U/math-neon" -j"$(nproc 2>/dev/null || echo 4)" install
# NO_DEBUG drops vitaGL's error-string paths; NO_SPLASHSCREEN drops the startup logo. Neither belongs in
# a build that boots straight into a game.
step vita-gl           make -C "$U/vitaGL" -j"$(nproc 2>/dev/null || echo 4)" NO_DEBUG=1 NO_SPLASHSCREEN=1 install

printf '%s' "$WANT" > "$STAMP"
echo "    vitaGL and math-neon installed in $VITASDK"
