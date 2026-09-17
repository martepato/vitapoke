#!/usr/bin/env bash
# stage.sh WORK : lay out the build tree in WORK (port sources + third-party + fetched upstream), fill
# in the placeholders that carry build-time paths and target flags into the staged sources.
#
#   @WORK@        the staged tree's own path
#   @TOOLBIN@     the toolchain's tool prefix, path included (.../bin/arm-vita-eabi-)
#   @TARGETCC@    compiler flags the whole build needs for this console; see the note below
#   @SDKBUILD@    the DS SDK's build-target define
#   @BUILDMAK@    the make fragment providing CC/AR and the object rules
#   @SDKINC@      VitaSDK's own include directory
#   @OVERLAYLD@   the linker script the overlay layout is built from (still to be written)
source "$(dirname "$0")/common.sh"
WORK=$1; T="$WORK/test_out"

# The CPU flags match what VitaSDK's compiler already defaults to, and are stated so a differently
# configured toolchain still builds the same code.
#
# -fno-short-enums is load-bearing. The ARM EABI makes an enum the smallest type that fits, so
# `enum { A, B }` is one byte and every struct holding one changes size -- silently, and everywhere.
# The DS SDK and the game are written for four-byte enums: libntr asserts struct sizes that depend on
# it, and the 512 KB save file has a fixed layout that enum-bearing structs are written into, so a
# shrunk enum would produce saves incompatible with a real DS rather than a compile error.
#
# stdlib.h and ctype.h are force-included because libntr's nitro/card/backup.h calls malloc and free,
# and fs_file.c calls tolower, without declaring them -- each relies on the including translation unit
# having done so.
#
# -Wno-incompatible-pointer-types puts a diagnostic GCC 15 promoted to an error back to a warning:
# this is decompiled code full of casts the original compiler accepted.
TARGETCC="-mtune=cortex-a9 -mfpu=neon -fno-short-enums -Wno-incompatible-pointer-types -include stdlib.h -include ctype.h"
SDKBUILD="SDK_BUILD_VITA"
BUILDMAK="$T/build/vita.mak"
SDKINC="$VITASDK/arm-vita-eabi/include"
OVERLAYLD="$T/build/overlays.ld"

mkdir -p "$T" "$WORK/melon"
rsync -a "$ROOT/port/" "$T/"
rsync -a --exclude README.md "$ROOT/third_party/melonDS/" "$WORK/melon/"
{ grep -rlI -e '@WORK@' -e '@TOOLBIN@' -e '@TARGETCC@' -e '@SDKBUILD@' -e '@BUILDMAK@' -e '@SDKINC@' -e '@OVERLAYLD@' "$T" || true; } | while read -r f; do
  sed -i.bak -e "s#@WORK@#$T#g" -e "s#@TOOLBIN@#$TOOLBIN#g" -e "s#@TARGETCC@#$TARGETCC#g" \
             -e "s#@SDKBUILD@#$SDKBUILD#g" -e "s#@BUILDMAK@#$BUILDMAK#g" -e "s#@SDKINC@#$SDKINC#g" \
             -e "s#@OVERLAYLD@#$OVERLAYLD#g" "$f" && rm -f "$f.bak"
done
