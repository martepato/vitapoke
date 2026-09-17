#!/usr/bin/env bash
# toolchain-vita.sh : download the pinned prebuilt VitaSDK for this computer into .cache/vitasdk.
# Pinned to one snapshot so every build uses the same compiler. Set VITASDK yourself to use another install.
#
# This deliberately does not use vdpm or bootstrap-vitasdk.sh. Both resolve a channel through
# vitasdk.org, which makes the compiler you get depend on when you built; the whole point of pinning is
# that it does not. The snapshot below is one immutable vitasdk/autobuilds release, and its SHA-256
# comes from that release's own SHA256SUMS.
source "$(dirname "$0")/common.sh"
# vitasdk/autobuilds release, and the series channels.json calls "supported" at that release.
SNAPSHOT=sdk-snapshot-20260825.611.1
case "$(uname -s)-$(uname -m)" in
  Darwin-arm64)   ASSET=vitasdk-arm64-apple-darwin-2026-08-25_12-50-52.tar.bz2;  SHA=2e7b4be17cadd0655a77f5201e50922061b3a50e7156e4d69d12b439af2c43aa;;
  Darwin-x86_64)  ASSET=vitasdk-x86_64-apple-darwin-2026-08-25_12-51-28.tar.bz2; SHA=fae123b6bc8e161327ffbfe7cfe2abc068913661bdfc24354bfbe35d698a4074;;
  Linux-x86_64)   ASSET=vitasdk-x86_64-linux-gnu-2026-08-25_12-51-16.tar.bz2;    SHA=ff0e1aa1d968222a98836fcb90ed2cd3e6f5b53074e94d69f8057c718b607a06;;
  Linux-aarch64|Linux-arm64) ASSET=vitasdk-aarch64-linux-gnu-2026-08-25_12-51-18.tar.bz2; SHA=1e2e6054d024b1d76f73886a7df6a4f3307bf344132f06963ee34c376289099a;;
  *) die "no prebuilt VitaSDK for $(uname -s) $(uname -m). Install VitaSDK (https://vitasdk.org) and set VITASDK.";;
esac
DEST="$CACHE/vitasdk"
[ -x "$DEST/bin/arm-vita-eabi-gcc" ] && [ "$(cat "$DEST/.vitapoke-release" 2>/dev/null)" = "$SNAPSHOT $ASSET" ] && { echo "    VitaSDK $SNAPSHOT already installed"; exit 0; }
need curl "Install curl."; need tar "Install tar."
log "Downloading VitaSDK ($SNAPSHOT, about 100 MB, one time)"
mkdir -p "$CACHE"; TMP="$CACHE/$ASSET.part"
curl -fL --progress-bar -o "$TMP" "https://github.com/vitasdk/autobuilds/releases/download/$SNAPSHOT/$ASSET" || die "download failed (check your internet connection and run the command again)"
GOT=$(if command -v sha256sum >/dev/null; then sha256sum "$TMP"; else shasum -a 256 "$TMP"; fi | cut -d' ' -f1)
[ "$GOT" = "$SHA" ] || { rm -f "$TMP"; die "VitaSDK download is corrupted (SHA-256 mismatch); run the command again"; }
rm -rf "$DEST" "$CACHE/vitasdk.extract"; mkdir -p "$CACHE/vitasdk.extract"
tar xjf "$TMP" -C "$CACHE/vitasdk.extract" && mv "$CACHE/vitasdk.extract/vitasdk" "$DEST" && rm -rf "$CACHE/vitasdk.extract" "$TMP"
echo "$SNAPSHOT $ASSET" > "$DEST/.vitapoke-release"
"$DEST/bin/arm-vita-eabi-gcc" --version >/dev/null 2>&1 || die "VitaSDK was downloaded but does not run on this computer"
echo "    VitaSDK $SNAPSHOT installed in .cache/vitasdk"
