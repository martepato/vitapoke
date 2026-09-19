#!/usr/bin/env bash
# game.sh : build Pokémon Platinum's code for the PS Vita, as far as the port currently reaches.
#
# It stops where the port stops. What works today, and what each step is proving:
#
#   base       fetch the pinned decompilation and SDK replacement, stage the tree for ARM
#   generated  the decompilation's generated headers (no ROM needed)
#   sdk        libntr and libntrsystem compiled for ARM, archived, and filtered so the modules this
#              port replaces (OS threads, alarms, the file system, the card) are dropped
#   services   the DS's memory and register backing, its sound engine, the network and internal
#              libraries -- the pieces of the port that are not console specific
#   game       the game's own 1016 C files compiled for ARM, and its overlay data layout
#   render     the Vita renderer: the DS 2D compositor and the GPU layer
#   link       the application, and the VPK
#
# No ROM is needed to build. With one, its file system is unpacked into the VPK; without, the VPK
# reads a ROM from the memory card at run time instead.
#
#   scripts/game.sh              build and link, producing dist/vitapoke-platinum.vpk
source "$(dirname "$0")/common.sh"
# Several steps pipe a long build log through tail; without this the pipeline would report the
# exit status of tail and a failed compile would be recorded as ok.
set -o pipefail

[ -x "${TOOLBIN}gcc" ] || die "VitaSDK not installed. Run: ./build.sh setup"

# Where the game's data comes from.
#
# A ROM at roms/Platinum.nds is used without being asked for -- that directory exists for exactly
# this, is in .gitignore, and is where `./build.sh game` looks every time. --rom FILE overrides it.
# With neither, the build produces a VPK with no game data in it, which reads the ROM from the
# memory card at run time instead; that is what keeps a clone buildable, and the checks and CI
# runnable, by somebody who has no dump at all.
ROM=""
while [ $# -gt 0 ]; do case "$1" in
  --rom) ROM="$2"; shift 2;;
  *) die "usage: scripts/game.sh [--rom FILE]";;
esac; done
if [ -z "$ROM" ] && [ -f "$ROOT/roms/Platinum.nds" ]; then
  ROM="$ROOT/roms/Platinum.nds"
  log "Using the ROM in roms/ (pass --rom FILE to use another, or remove it to build without)"
fi
if [ -n "$ROM" ]; then
  [ -f "$ROM" ] || die "no such file: $ROM"
  ROM="$(cd "$(dirname "$ROM")" && pwd)/$(basename "$ROM")"
fi

WORK="$ROOT/.work/vita"; T="$WORK/test_out"; LOGS="$WORK/logs"; U="$CACHE/upstream"
mkdir -p "$LOGS"
done_(){ [ -f "$WORK/.stamp-$1" ]; }; mark(){ touch "$WORK/.stamp-$1"; }

# Which toolchain built this tree.
#
# The stamps below skip finished phases on a rerun, and the object files they stand for live in the
# staged tree. Neither records which compiler produced them, so somebody who builds with the pinned
# toolchain and then sets VITASDK to their own install -- which the build supports, and which is the
# whole reason this check exists -- would otherwise link objects from two different compilers and
# find out at the oddest possible moment. The path, the compiler's version and the pinned GPU
# libraries' revisions are enough to tell the two apart; when it changes, everything compiled goes.
TOOLCHAIN_ID="$VITASDK|$("${TOOLBIN}gcc" -dumpversion 2>/dev/null || echo unknown)|$(cat "$VITASDK/.vitapoke-deps" 2>/dev/null || true)"
if [ -f "$WORK/.toolchain" ] && [ "$(cat "$WORK/.toolchain")" != "$TOOLCHAIN_ID" ]; then
  log "The toolchain changed since this tree was built: rebuilding it from the start"
  rm -rf "$T" "$WORK"/.stamp-*
fi
printf '%s' "$TOOLCHAIN_ID" > "$WORK/.toolchain"

if ! done_ base; then
  log "Fetching pinned upstream sources"
  "$ROOT/scripts/fetch.sh" libntr libntrsystem libntrdwc libntrwifi libvct metang pokeplatinum
  log "Staging the build tree for ARM in .work/vita"
  rm -rf "$T"; "$ROOT/scripts/stage.sh" "$WORK"
  for spec in "libntr native-graphics/libntr" "libntrsystem native-probe/libntrsystem" \
              "libntrdwc native-probe/libntrdwc" "libntrwifi native-probe/libntrwifi" \
              "libvct native-probe/libvct" "metang native-probe/metang"; do
    set -- $spec; rsync -a --exclude .git "$U/$1/" "$T/$2/"
  done
  rsync -a "$U/pokeplatinum/" "$T/native-probe/pokeplatinum/"   # keeps .git: gen-game-tables.py uses git ls-tree
  (cd "$T/native-probe/pokeplatinum" && patch -p1 -s < "$ROOT/patches/pokeplatinum/local-edits.patch")
  # Who owns a geometry command list: the producer. See the patch's own comment.
  (cd "$T/native-probe/libntrsystem" && patch -p1 -s < "$ROOT/patches/libntrsystem/g3d-command-ownership.patch")
  mark base
fi

# The port's own sources, every run.
#
# The staging above happens once, because it is what lays the tree out and everything else in .work is
# built on top of it. That meant an edit under port/ did not reach the build at all: the compiler saw
# the copy staged the first time, and the only way to pick the change up was to delete the tree and
# rebuild all thousand-odd files. Re-running the staging without the rm is the whole fix -- rsync
# copies what changed and leaves the built tree alone, and stage.sh only substitutes its placeholders
# in files that still have them, which are exactly the ones it has just copied.
step restage      "$ROOT/scripts/stage.sh" "$WORK"

if ! done_ generated; then
  log "Generating headers from the decompilation"
  N="$T/native-probe"; P="$N/pokeplatinum"; G="$N/generated"
  rm -rf "$G"; mkdir -p "$G"
  step genheaders   bash -c "cd '$N' && python3 genheaders.py"
  step source-meta  bash -c "cd '$N' && python3 gen-source-metadata.py"
  step game-tables  bash -c "cd '$N' && python3 gen-game-tables.py"
  mkdir -p "$G/nitro/fx" "$G/res/fonts" "$G/res/graphics/battle/healthbox" "$G/res/words"
  step fx-const     python3 "$T/native-graphics/libntr/gen/nitro/fx/gen_fx_const.py" \
                            "$T/native-graphics/libntr/gen/nitro/fx/fx_const.csv" "$G/nitro/fx/fx_const.h"
  # Two small bitmaps as C arrays: the same output as the decompilation's own tool, without libpng.
  step embed-cursor python3 "$ROOT/scripts/png_embed.py" "$P/res/fonts/arrow_cursor.png" \
                            "$G/res/fonts/arrow_cursor.4bpp" sArrowCursorBitmap
  step embed-health python3 "$ROOT/scripts/png_embed.py" \
                            "$P/res/graphics/battle/healthbox/healthbox_parts.png" \
                            "$G/res/graphics/battle/healthbox/healthbox_parts.4bpp" sHealthBoxPartsBitmap
  printf '#define word_bank_o 0\n' > "$G/res/words/word_bank.naix"
  mark generated
fi

if ! done_ services; then
  log "Building the DS memory backing, its registers and the sound engine"
  # storage.c and the register storage are generated from libntr's own simulator variables, so the
  # DS's memory map is whatever libntr says it is rather than a copy that can drift from it.
  step backing       bash -c "cd '$T/native-probe/backing' && python3 generate.py && python3 generate_registers.py && make storage.o memory_layout.o device_registers.o && ${TOOLBIN}ld -r storage.o memory_layout.o device_registers.o -o native-backing.o"
  step gfx-registers bash -c "cd '$T/native-graphics/alias-proof' && python3 generate_registers.py && make graphics_registers.o"
  step trainer-ai    bash -c "cd '$T/native-core-proof' && python3 rebuild.py"
  step offline       make -C "$T/native-offline" offline.o
  step audio         make -C "$T/native-audio-sound" audio_bank.o audio_loader.o audio_backend.o \
                          audio_seq.o audio_exchannel.o audio_channel.o audio_engine.o mic_pm.o
  step network       bash -c "cd '$T/native-probe' && python3 compile-network.py"
  step internal      bash -c "cd '$T/native-probe' && python3 compile-internal.py"
  mark services
fi

if ! done_ sdk; then
  log "Compiling the DS SDK replacement for ARM"
  # compile.py reports per-file failures rather than stopping, because the modules this port replaces
  # are dropped by filter-sdk.py straight afterwards and do not have to build. sdk-filter is what
  # makes that safe: a module that failed here and is *not* dropped shows up as a link error later.
  step sdk-compile   bash -c "cd '$T/native-sdk-probe' && python3 compile.py | tail -40"
  step sdk-archive   python3 "$ROOT/scripts/sdk_archive.py" "$T/native-sdk-probe"
  step sdk-filter    bash -c "cd '$T/native-audio-app' && python3 filter-sdk.py && cp -f libsdk-filtered.a libsdk-filtered.a.base"
  mark sdk
fi

O="$T/native-audio-app/overlays"

if ! done_ game; then
  log "Compiling the game's own code for ARM"
  step ov-generate  bash -c "cd '$O' && python3 generate.py"
  step ov-build     bash -c "cd '$O' && python3 build.py 2>&1 | tail -40"
  step ov-internal  bash -c "cd '$O' && python3 internal.py && cp -f libplatinum-overlays.a libplatinum-overlays.a.base"
  mark game
fi
step ov-link      bash -c "cd '$O' && python3 gen-link.py"

log "Building the renderer"
step renderer     make -C "$T/native-vita-render" libnative-render-vita.a

# The game's data, unpacked from the ROM into files the game can open by name. Redone whenever the
# ROM changes, and skipped entirely when there is none.
ASSETS=""
if [ -n "$ROM" ]; then
  ASSETS="$WORK/assets"
  if [ ! -f "$ASSETS/.stamp" ] || [ "$(cat "$ASSETS/.stamp" 2>/dev/null)" != "$ROM $(sha1 "$ROM")" ]; then
    log "Unpacking the ROM's file system for the VPK"
    rm -rf "$ASSETS"; mkdir -p "$ASSETS"
    python3 "$ROOT/scripts/extract_assets.py" "$ROM" "$ASSETS" || die "could not unpack $ROM"
    printf '%s' "$ROM $(sha1 "$ROM")" > "$ASSETS/.stamp"
  else
    echo "    the ROM's file system is already unpacked"
  fi
fi

log "Linking the application"
A="$T/native-audio-app"
# Removed first so the VPK is always repacked: whether the assets are in it is an argument rather
# than a file make can compare timestamps against, and a stale VPK from the other choice would be
# indistinguishable from a fresh one.
rm -f "$A/vitapoke-platinum.vpk"
# VITAPOKE_MALLOC_GUARD=1 builds the diagnostic allocator in: every block gets a guard word either
# side, and every 300 frames the log names the call sites holding the most memory and how much each
# has gained since the last report. That last column is what finds a leak -- the biggest number in
# the list is the game's own arena and always will be; the one that climbs by the same amount every
# report is the bug. It costs 32 bytes and a list walk per allocation, so it is not a build to play.
step link         make -C "$A" vitapoke-platinum.vpk ASSET_DIR="$ASSETS" \
                       MALLOC_GUARD="${VITAPOKE_MALLOC_GUARD:-}"
OUT="$ROOT/dist"; mkdir -p "$OUT"
cp -f "$A/vitapoke-platinum.vpk" "$OUT/vitapoke-platinum.vpk"
if [ -n "$ASSETS" ]; then
  log "Done: dist/vitapoke-platinum.vpk ($(du -h "$OUT/vitapoke-platinum.vpk" | cut -f1)), with the game's data in it"
  log "That VPK contains the game. It is yours to keep, and not to share."
else
  log "Done: dist/vitapoke-platinum.vpk (no game data; it reads a ROM from ux0:data/vitapoke)"
fi
