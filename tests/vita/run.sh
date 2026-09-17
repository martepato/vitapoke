#!/usr/bin/env bash
# tests/vita/run.sh : checks for the Vita port that do not need a ROM.
#
# The main suite (tests/run.sh) builds both games from your ROM and replays scenarios in an emulator.
# These checks are cheaper and come first: they prove the Vita toolchain is installed, that the DS
# interfaces the platform layer implements still match libntr and psp2, and that the parts of the port
# that exist compile and link into a valid Vita executable.
#
# A toolchain bump is the thing this catches. psp2's headers move under vitapoke's feet -- a renamed
# constant, an argument added to a thread call -- and without these checks that surfaces several
# phases into a full build, or not until the console refuses to load the module.
#
# The DS check needs libntr's headers, so it fetches the pinned copy if it is not already in .cache,
# and generates the one header libntr expects the build to produce (nitro/fx/fx_const.h).
#
# Usage: tests/vita/run.sh
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
source "$ROOT/scripts/common.sh"
export PATH="$VITASDK/bin:$PATH"

OUT="$ROOT/.work/vita-tests"
rm -rf "$OUT"; mkdir -p "$OUT"
LOGS="$OUT"          # `step`, from common.sh, writes here; `check` below does its own logging
U="$CACHE/upstream"

pass=0
check(){ printf '    %-28s' "$1"; shift; if "$@" > "$OUT/$$.log" 2>&1; then echo ok; pass=$((pass+1)); else echo FAILED; sed 's/^/        /' "$OUT/$$.log" >&2; exit 1; fi; }

log "Vita port checks"

[ -x "$VITASDK/bin/arm-vita-eabi-gcc" ] || die "VitaSDK not found at $VITASDK. Run: ./build-vita.sh setup"
[ -f "$VITASDK/arm-vita-eabi/lib/libvitaGL.a" ] || die "vitaGL not built. Run: ./build-vita.sh setup"

CFLAGS=(-O2 -mtune=cortex-a9 -mfpu=neon -Wl,-q -I"$ROOT/port/vita/include")
STUBS=(-lSceDisplay_stub -lSceCtrl_stub -lScePower_stub -lSceRtc_stub -lSceTouch_stub
       -lSceAppMgr_stub -lSceNet_stub -lSceLibKernel_stub)

# The platform layer: the DS interfaces the game and libntr call, implemented on psp2.
# SDK_X86 is what the PSP build passes too: in libntr it means "not real DS hardware", and it selects
# the same portable code paths on both consoles.
NITRO=(-DPM_KEEP_ASSERTS -DSDK_PORT -DSDK_X86 -DSDK_BUILD_VITA -DSDK_VERSION_MAJOR=4 -DSDK_TS
       -DSDK_4M -DSDK_FINALROM -DNNS_FINALROM)
PLATFORM=("$ROOT/port/vita/os_core.c" "$ROOT/port/vita/os_alarm.c" "$ROOT/port/vita/os_thread.c"
          "$ROOT/port/vita/input.c" "$ROOT/port/vita/sdl_sync.c")

"$ROOT/scripts/fetch.sh" libntr
GEN="$ROOT/.work/vita-gen"
mkdir -p "$GEN/nitro/fx"
check "fx_const.h" python3 "$U/libntr/gen/nitro/fx/gen_fx_const.py" \
      "$U/libntr/gen/nitro/fx/fx_const.csv" "$GEN/nitro/fx/fx_const.h"

NITRO_INC=(-I"$U/libntr/include" -I"$GEN" -I"$ROOT/port/vita/sdl2-shim")

check "ds surface" arm-vita-eabi-gcc "${CFLAGS[@]}" -std=gnu99 "${NITRO[@]}" "${NITRO_INC[@]}" \
      -o "$OUT/ds.elf" "$ROOT/tests/vita/ds_surface.c" "${PLATFORM[@]}" "${STUBS[@]}"
check "ds velf" vita-elf-create "$OUT/ds.elf" "$OUT/ds.velf"

# vitaGL with the features the renderer needs: paletted textures for the DS tile atlases, stencil for
# the window/priority masks, and render-to-texture for the two engine buffers and the 3D buffer. This is
# what the GXM renderer has to reproduce, so a toolchain change that takes one of them away should fail
# here rather than halfway through writing it.
check "vitaGL features" arm-vita-eabi-gcc "${CFLAGS[@]}" -std=gnu99 \
      -o "$OUT/vgl.elf" "$ROOT/tests/vita/vitagl_features.c" "$ROOT/port/vita/shark_stub.c" \
      -lvitaGL -lmathneon -lSceGxm_stub -lSceCommonDialog_stub -lSceKernelDmacMgr_stub \
      -lSceSysmodule_stub -lSceShaccCg_stub_weak -lSceTouch_stub -lstdc++ -lm "${STUBS[@]}"
check "vitaGL velf" vita-elf-create "$OUT/vgl.elf" "$OUT/vgl.velf"

rm -f "$OUT/$$.log"
log "$pass checks passed"
