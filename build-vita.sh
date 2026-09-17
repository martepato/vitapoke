#!/usr/bin/env bash
# vitapoke build entry point for the PS Vita. See docs/VITA.md.
#   ./build-vita.sh setup     check requirements, download the pinned VitaSDK, build vitaGL
#   ./build-vita.sh check     run the Vita checks that do not need a ROM (tests/vita/run.sh)
#   ./build-vita.sh clean     remove the Vita build output (downloads in .cache are kept)
#
# Building a game is not wired up yet: the port's GPU, audio and packaging layers are still being
# written, and the parts that exist are the ones `check` exercises. `./build.sh platinum|soulsilver`
# continues to build for the PSP. docs/VITA.md tracks what is left.
source "$(dirname "$0")/scripts/common.sh"

CMD="${1:-}"; shift || true
while [ $# -gt 0 ]; do case "$1" in
  *) die "unknown option $1";;
esac; done

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
  clean)
    rm -rf "$ROOT/.work/vita-tests" "$ROOT/dist/vita"
    log "Removed the Vita build output (downloads in .cache kept)"
    ;;
  platinum|soulsilver)
    die "the Vita build cannot build a game yet: the renderer, the audio backend and VPK packaging are still being written (see docs/VITA.md). ./build.sh $CMD --rom <file.nds> builds it for the PSP."
    ;;
  *)
    die "usage: ./build-vita.sh setup|check|clean   (see docs/VITA.md)"
    ;;
esac
