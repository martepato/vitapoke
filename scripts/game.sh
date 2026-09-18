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
# No ROM is needed to build: the port reads everything it needs from yours at startup instead.
#
#   scripts/game.sh              build and link, producing dist/vitapoke-platinum.vpk
source "$(dirname "$0")/common.sh"
# Several steps pipe a long build log through tail; without this the pipeline would report the
# exit status of tail and a failed compile would be recorded as ok.
set -o pipefail

[ -x "${TOOLBIN}gcc" ] || die "VitaSDK not installed. Run: ./build.sh setup"

[ $# -eq 0 ] || die "unknown option $1"

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
  mark base
fi

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
qol="/* generated by scripts/game.sh: quality-of-life switches (1 = on). */
#ifndef VITAPOKE_QOL_H
#define VITAPOKE_QOL_H
#define VITAPOKE_QOL_INSTANT_TEXT ${VITAPOKE_QOL_INSTANT_TEXT:-1}
#define VITAPOKE_QOL_TRADE_EVOS ${VITAPOKE_QOL_TRADE_EVOS:-1}
#define VITAPOKE_QOL_REPEL_PROMPT ${VITAPOKE_QOL_REPEL_PROMPT:-1}
#define VITAPOKE_QOL_FORGET_HMS ${VITAPOKE_QOL_FORGET_HMS:-1}
#define VITAPOKE_QOL_MOVE_BUFFS ${VITAPOKE_QOL_MOVE_BUFFS:-1}
#endif
"
mkdir -p "$O/include"; printf '%s' "$qol" > "$O/include/vitapoke_qol.h"

if ! done_ game; then
  log "Compiling the game's own code for ARM"
  step ov-generate  bash -c "cd '$O' && python3 generate.py"
  step ov-build     bash -c "cd '$O' && python3 build.py 2>&1 | tail -40"
  # The quality-of-life changes are a patch against the overlay sources' local copies, which
  # build.py has just written; the nine files it touches are then recompiled in place.
  step qol-patch    bash -c "cd '$O/source' && patch -p1 -s < '$ROOT/patches/platinum/qol-overlay-sources.patch'"
  step ov-internal  bash -c "cd '$O' && python3 internal.py && cp -f libplatinum-overlays.a libplatinum-overlays.a.base"
  mark game
fi
# Rebuilt every run so that a changed switch takes effect without a clean tree.
step qol-rebuild  bash -c "cd '$O' && python3 rebuild_obj.py text.c applications/bag/main.c \
                           game_options.c item.c overlay006/repel_step_update.c pokemon.c \
                           move_table.c applications/pokemon_summary_screen/main.c \
                           battle_sub_menus/battle_party.c"
step ov-link      bash -c "cd '$O' && python3 gen-link.py"

log "Building the renderer"
step renderer     make -C "$T/native-vita-render" libnative-render-vita.a

log "Linking the application"
A="$T/native-audio-app"
step link         make -C "$A" vitapoke-platinum.vpk
OUT="$ROOT/dist"; mkdir -p "$OUT"
cp -f "$A/vitapoke-platinum.vpk" "$OUT/vitapoke-platinum.vpk"
log "Done: dist/vitapoke-platinum.vpk"
