#!/usr/bin/env bash
# vitapoke build entry point for the PS Vita. See docs/VITA.md.
#   ./build-vita.sh setup     check requirements, download the pinned VitaSDK, build vitaGL
#   ./build-vita.sh check     run the Vita checks that do not need a ROM (tests/vita/run.sh)
#   ./build-vita.sh emu-check run the platform layer's runtime checks in the Vita3K emulator
#   ./build-vita.sh game      compile the SDK and the game's own code for ARM (scripts/vita.sh)
#   ./build-vita.sh clean     remove the Vita build output (downloads in .cache are kept)
#
# `game` compiles everything that does not need the renderer: the DS SDK replacement and all 1016 of
# the game's own C files, for ARM. Linking is not wired up yet -- that needs the renderer, the audio
# backend and the overlay layout. docs/VITA.md tracks what is left.
source "$(dirname "$0")/scripts/common.sh"

CMD="${1:-}"; shift || true
# Remaining arguments are passed to the subcommand (game takes --rom).
case "$CMD" in
  game) ;;
  *) [ $# -eq 0 ] || die "unknown option $1";;
esac

case "$CMD" in
  setup)
    # Only what setup runs: fetch the sources (git), download and unpack the toolchain (curl, tar) and
    # build vitaGL (make). The PSP build's extra tools -- python3, patch, rsync -- stage a game tree,
    # which this does not do.
    bash "$ROOT/scripts/prereqs.sh" git make curl tar
    if [ "$VITAPOKE_OWN_TOOLCHAIN" = 1 ]; then
      [ -x "$VITASDK/bin/arm-vita-eabi-gcc" ] || die "VitaSDK not found at \$VITASDK=$VITASDK (unset VITASDK to use the automatic download)."
    else
      bash "$ROOT/scripts/toolchain-vita.sh"
    fi
    bash "$ROOT/scripts/deps-vita.sh"
    log "Setup complete. Next: ./build-vita.sh check"
    ;;
  check)
    [ -x "$VITASDK/bin/arm-vita-eabi-gcc" ] || die "VitaSDK not installed. Run: ./build-vita.sh setup"
    exec bash "$ROOT/tests/vita/run.sh"
    ;;
  game)
    [ -x "$VITASDK/bin/arm-vita-eabi-gcc" ] || die "VitaSDK not installed. Run: ./build-vita.sh setup"
    shift 0
    exec bash "$ROOT/scripts/vita.sh" "$@"
    ;;
  emu-check)
    [ -x "$VITASDK/bin/arm-vita-eabi-gcc" ] || die "VitaSDK not installed. Run: ./build-vita.sh setup"
    exec bash "$ROOT/tests/vita/vita3k.sh"
    ;;
  clean)
    rm -rf "$ROOT/.work/vita-tests" "$ROOT/.work/vita-gen" "$ROOT/.work/vita3k" "$ROOT/.work/vita" "$ROOT/dist/vita"
    log "Removed the Vita build output (downloads in .cache kept)"
    ;;
  platinum|soulsilver)
    die "use ./build-vita.sh game to compile for the Vita. A linked, runnable build needs the renderer and the audio backend (see docs/VITA.md)."
    ;;
  *)
    die "usage: ./build-vita.sh setup|check|game|emu-check|clean   (see docs/VITA.md)"
    ;;
esac
