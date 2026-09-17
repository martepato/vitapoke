#!/usr/bin/env bash
# vitapoke build entry point. See docs/VITA.md.
#   ./build.sh setup     check requirements, download the pinned VitaSDK, build vitaGL
#   ./build.sh check     run the Vita checks that do not need a ROM (tests/vita/run.sh)
#   ./build.sh emu-check run the platform layer's runtime checks in the Vita3K emulator
#   ./build.sh boot      boot the built game in the Vita3K emulator and print its log
#   ./build.sh game      build the game and link the VPK (scripts/game.sh)
#   ./build.sh clean     remove the Vita build output (downloads in .cache are kept)
#
# `game` does the whole build: the DS SDK replacement, all 1016 of the game's own C files, the Vita
# platform layer and renderer, and the link into dist/vitapoke-platinum.vpk. No ROM is needed to
# build -- you put yours on the memory card and the port reads it there. docs/VITA.md tracks what
# does and does not work yet.
source "$(dirname "$0")/scripts/common.sh"

CMD="${1:-}"; shift || true
# Only `boot` takes arguments of its own (--rom, --seconds).
case "$CMD" in
  boot) ;;
  *) [ $# -eq 0 ] || die "unknown option $1";;
esac

case "$CMD" in
  setup)
    # Only what setup runs: fetch the sources (git), download and unpack the toolchain (curl, tar) and
    # build vitaGL (make). python3, patch and rsync are for staging a game tree, which this does not do.
    bash "$ROOT/scripts/prereqs.sh" git make curl tar
    if [ "$VITAPOKE_OWN_TOOLCHAIN" = 1 ]; then
      [ -x "$VITASDK/bin/arm-vita-eabi-gcc" ] || die "VitaSDK not found at \$VITASDK=$VITASDK (unset VITASDK to use the automatic download)."
    else
      bash "$ROOT/scripts/toolchain.sh"
    fi
    bash "$ROOT/scripts/deps.sh"
    log "Setup complete. Next: ./build.sh check"
    ;;
  check)
    [ -x "$VITASDK/bin/arm-vita-eabi-gcc" ] || die "VitaSDK not installed. Run: ./build.sh setup"
    exec bash "$ROOT/tests/vita/run.sh"
    ;;
  game)
    [ -x "$VITASDK/bin/arm-vita-eabi-gcc" ] || die "VitaSDK not installed. Run: ./build.sh setup"
    exec bash "$ROOT/scripts/game.sh"
    ;;
  emu-check)
    [ -x "$VITASDK/bin/arm-vita-eabi-gcc" ] || die "VitaSDK not installed. Run: ./build.sh setup"
    exec bash "$ROOT/tests/vita/vita3k.sh"
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
