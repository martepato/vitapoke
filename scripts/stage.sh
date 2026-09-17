#!/usr/bin/env bash
# stage.sh WORK : lay out the build tree in WORK (port sources + third-party + fetched upstream), fill in path placeholders.
#
# The staged tree is per-console. PSPOKE_TARGET selects which -- psp (the default) or vita -- and the
# placeholders below are what the difference is made of, so the sources in port/ stay single-copy and a
# change to the build lands on both consoles at once.
#
#   @WORK@       the staged tree's own path
#   @TOOLBIN@    the toolchain's tool prefix, path included: .../bin/psp- or .../bin/arm-vita-eabi-
#   @TARGETCC@   compiler flags that only make sense for one CPU (-G0 on the PSP's MIPS, NEON on ARM)
#   @SDKBUILD@   the SDK's build-target define: SDK_BUILD_PSP or SDK_BUILD_VITA
#   @BUILDMAK@   the make fragment providing CC/AR and the object rules for this console
#   @SDKINC@     the console SDK's own include directory
#   @PRXLINKFILE@ the base linker script the overlay layout is built from (PSP only so far)
#   @PSPDEV@     the PSP toolchain root (PSP only; nothing in the Vita build refers to it)
#   @PPSSPP@     the headless PSP emulator used by the test scenarios
source "$(dirname "$0")/common.sh"
WORK=$1; T="$WORK/test_out"
TARGET="$PSPOKE_TARGET"   # set and validated by common.sh

case "$TARGET" in
  psp)
    # -G0 puts everything in the normal data sections rather than MIPS small data, which the PSP
    # build needs because the game's data outgrows the 64 KiB $gp window.
    TARGETCC="-G0"
    SDKBUILD="SDK_BUILD_PSP"
    BUILDMAK="$T/build/psp.mak"
    SDKINC="$PSPDEV/psp/sdk/include"
    PRXLINKFILE="$PSPDEV/psp/sdk/lib/linkfile.prx"
    ;;
  vita)
    # The Vita is a Cortex-A9 with NEON. There is no small-data window to opt out of, so -G0 has no
    # counterpart here; the CPU flags match what VitaSDK's compiler already defaults to and are stated
    # so a differently configured toolchain still builds the same code.
    #
    # stdlib.h comes first because libntr's nitro/card/backup.h calls malloc and free without declaring
    # them, and relies on the including translation unit having done so. PSPDEV's headers happen to
    # declare them through another path; newlib on ARM does not.
    #
    # -fno-short-enums is load-bearing. The ARM EABI makes an enum the smallest type that fits, so
    # `enum { A, B }` is one byte and every struct holding one changes size -- silently, and
    # everywhere. The DS SDK and the game are written for four-byte enums: libntr asserts struct sizes
    # that depend on it, and the 512 KB save file has a fixed layout that enum-bearing structs are
    # written into, so a shrunk enum would produce saves incompatible with the DS and the PSP build
    # rather than a compile error.
    #
    # ctype.h for the same reason as stdlib.h. -Wno-incompatible-pointer-types puts a diagnostic GCC 15
    # promoted to an error back to a warning: this is decompiled code full of casts the original
    # compiler accepted, and the SoulSilver half of the tree already passes the same flag.
    TARGETCC="-mtune=cortex-a9 -mfpu=neon -fno-short-enums -Wno-incompatible-pointer-types -include stdlib.h -include ctype.h"
    SDKBUILD="SDK_BUILD_VITA"
    BUILDMAK="$T/build/vita.mak"
    SDKINC="$VITASDK/arm-vita-eabi/include"
    # The Vita has no PRX linker script to edit; its overlay layout is still to be written.
    PRXLINKFILE="$T/build/vita-overlays.ld"
    ;;
  *) die "PSPOKE_TARGET must be psp or vita (got '$TARGET')";;
esac

mkdir -p "$T" "$WORK/tools" "$WORK/melon"
rsync -a "$ROOT/port/" "$T/"
rsync -a --exclude README.md "$ROOT/third_party/melonDS/" "$WORK/melon/"
cp -f "$ROOT/scripts/check_native_pbp.py" "$WORK/tools/check_native_pbp.py"
{ grep -rlI -e '@WORK@' -e '@PSPDEV@' -e '@PPSSPP@' -e '@TOOLBIN@' -e '@TARGETCC@' -e '@SDKBUILD@' -e '@BUILDMAK@' -e '@SDKINC@' -e '@PRXLINKFILE@' "$T" || true; } | while read -r f; do
  sed -i.bak -e "s#@WORK@#$T#g" -e "s#@PSPDEV@#$PSPDEV#g" -e "s#@PPSSPP@#${PPSSPP_HEADLESS:-PPSSPPHeadless}#g" \
             -e "s#@TOOLBIN@#$TOOLBIN#g" -e "s#@TARGETCC@#$TARGETCC#g" -e "s#@SDKBUILD@#$SDKBUILD#g" \
             -e "s#@BUILDMAK@#$BUILDMAK#g" -e "s#@SDKINC@#$SDKINC#g" -e "s#@PRXLINKFILE@#$PRXLINKFILE#g" "$f" && rm -f "$f.bak"
done
