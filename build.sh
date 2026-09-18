#!/usr/bin/env bash
# vitapoke build entry point. See docs/VITA.md.
#   ./build.sh setup     check requirements, download the pinned VitaSDK, build vitaGL
#
# Set VITASDK to a VitaSDK you already have and the build uses that instead of downloading one; see
# "Using a VitaSDK you already have" in README.md for what it then writes into your install.
#   ./build.sh check     run the Vita checks that do not need a ROM (tests/vita/run.sh)
#   ./build.sh emu-check run the platform layer's runtime checks in the Vita3K emulator
#   ./build.sh boot      boot the built game in the Vita3K emulator and print its log
#   ./build.sh game      build the game and link the VPK (scripts/game.sh)
#                        a ROM at roms/Platinum.nds is unpacked into the VPK; --rom FILE overrides
#   ./build.sh clean     remove the Vita build output (downloads in .cache are kept)
#
# `game` does the whole build: the DS SDK replacement, all 1016 of the game's own C files, the Vita
# platform layer and renderer, and the link into dist/vitapoke-platinum.vpk. No ROM is needed to
# build -- you put yours on the memory card and the port reads it there. docs/VITA.md tracks what
# does and does not work yet.
source "$(dirname "$0")/scripts/common.sh"

CMD="${1:-}"; shift || true
# `boot` takes its own arguments (--rom, --seconds, --press, --shot), and `game` takes --rom.
case "$CMD" in
  boot|game) ;;
  *) [ $# -eq 0 ] || die "unknown option $1";;
esac

case "$CMD" in
  setup)
    # Only what setup runs: fetch the sources (git), download and unpack the toolchain (curl, tar) and
    # build vitaGL (make). python3, patch and rsync are for staging a game tree, which this does not do.
    bash "$ROOT/scripts/prereqs.sh" git make curl tar
    if [ "$VITAPOKE_OWN_TOOLCHAIN" = 1 ]; then
      [ -x "$VITASDK/bin/arm-vita-eabi-gcc" ] ||
        die "no arm-vita-eabi-gcc under \$VITASDK=$VITASDK.
VITASDK should be the directory that has bin/arm-vita-eabi-gcc in it -- the same value VitaSDK's own
instructions ask you to export. Unset it to use the pinned toolchain the build downloads instead."
      log "Using your VitaSDK at $VITASDK ($("$VITASDK/bin/arm-vita-eabi-gcc" -dumpversion))"
    else
      bash "$ROOT/scripts/toolchain.sh"
    fi
    bash "$ROOT/scripts/deps.sh"
    log "Setup complete. Next: ./build.sh check"
    ;;
  check|game|emu-check)
    # One message for all three: whichever toolchain is in play, saying which one was looked for is
    # the difference between a puzzle and a typo.
    [ -x "$VITASDK/bin/arm-vita-eabi-gcc" ] || {
      if [ "$VITAPOKE_OWN_TOOLCHAIN" = 1 ]; then
        die "no arm-vita-eabi-gcc under \$VITASDK=$VITASDK. Check the path, or unset VITASDK to use
the pinned toolchain the build downloads."
      fi
      die "VitaSDK not installed. Run: ./build.sh setup"
    }
    case "$CMD" in
      check)     exec bash "$ROOT/tests/vita/run.sh";;
      game)      exec bash "$ROOT/scripts/game.sh" "$@";;
      emu-check) exec bash "$ROOT/tests/vita/vita3k.sh";;
    esac
    ;;
  boot)
    exec bash "$ROOT/tests/vita/boot.sh" "$@"
    ;;
  clean)
    rm -rf "$ROOT/.work/vita-tests" "$ROOT/.work/vita-gen" "$ROOT/.work/vita3k" "$ROOT/.work/vita" "$ROOT/dist"
    log "Removed the Vita build output (downloads in .cache kept)"
    ;;
  *)
    die "usage: ./build.sh setup|check|game|boot|emu-check|clean   (see docs/VITA.md)"
    ;;
esac
