# Shared helpers for pspoke build scripts (sourced, not run).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CACHE="${PSPPOKE_CACHE:-$ROOT/.cache}"
# PSP toolchain: your own install if PSPDEV is set, otherwise the pinned copy scripts/toolchain.sh downloads.
if [ -n "${PSPDEV:-}" ]; then PSPPOKE_OWN_TOOLCHAIN=1; else PSPPOKE_OWN_TOOLCHAIN=0; PSPDEV="$CACHE/pspdev"; fi
export PSPDEV PSPPOKE_OWN_TOOLCHAIN
export PATH="$PSPDEV/bin:$PATH"
# Vita toolchain: your own install if VITASDK is set, otherwise the pinned copy scripts/toolchain-vita.sh
# downloads. Only the Vita build (build-vita.sh) puts it on PATH; the PSP build never sees it.
# VITAPOKE_OWN_TOOLCHAIN is decided once and inherited: build-vita.sh exports VITASDK to its children,
# so a child that re-derived this from VITASDK alone would decide the download was the user's own install.
if [ -z "${VITAPOKE_OWN_TOOLCHAIN:-}" ]; then
  if [ -n "${VITASDK:-}" ]; then VITAPOKE_OWN_TOOLCHAIN=1; else VITAPOKE_OWN_TOOLCHAIN=0; VITASDK="$CACHE/vitasdk"; fi
fi
: "${VITASDK:=$CACHE/vitasdk}"
export VITASDK VITAPOKE_OWN_TOOLCHAIN
log(){ printf '\033[1m==> %s\033[0m\n' "$*"; }
die(){ printf 'error: %s\n' "$*" >&2; exit 1; }
# step NAME CMD... : run CMD in a subshell, keep its output in $LOGS/NAME.log, stop on failure.
step(){ local n=$1; shift; printf '    %-22s' "$n"; if ( "$@" ) > "$LOGS/$n.log" 2>&1; then echo ok; else echo "FAILED (see $LOGS/$n.log)"; tail -15 "$LOGS/$n.log" >&2; exit 1; fi; }
need(){ command -v "$1" >/dev/null 2>&1 || die "$1 not found. $2"; }
sha1(){ if command -v sha1sum >/dev/null; then sha1sum "$1" | cut -d' ' -f1; else shasum -a 1 "$1" | cut -d' ' -f1; fi; }
