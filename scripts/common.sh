# Shared helpers for vitapoke build scripts (sourced, not run).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CACHE="${VITAPOKE_CACHE:-$ROOT/.cache}"

# Vita toolchain: your own install if VITASDK is set, otherwise the pinned copy scripts/toolchain.sh
# downloads into .cache/vitasdk. VITAPOKE_OWN_TOOLCHAIN is decided once and inherited, because build.sh
# exports VITASDK to its children and a child re-deriving this from VITASDK alone would mistake the
# download for the user's own install.
if [ -z "${VITAPOKE_OWN_TOOLCHAIN:-}" ]; then
  if [ -n "${VITASDK:-}" ]; then VITAPOKE_OWN_TOOLCHAIN=1; else VITAPOKE_OWN_TOOLCHAIN=0; VITASDK="$CACHE/vitasdk"; fi
fi
: "${VITASDK:=$CACHE/vitasdk}"
# TOOLBIN is the toolchain's tool prefix with its path, so a build step runs "${TOOLBIN}ar" rather than
# naming the compiler. scripts/stage.sh substitutes the same value into the staged tree as @TOOLBIN@.
TOOLBIN="$VITASDK/bin/arm-vita-eabi-"
export VITASDK VITAPOKE_OWN_TOOLCHAIN TOOLBIN

log(){ printf '\033[1m==> %s\033[0m\n' "$*"; }
die(){ printf 'error: %s\n' "$*" >&2; exit 1; }
# step NAME CMD... : run CMD in a subshell, keep its output in $LOGS/NAME.log, stop on failure.
step(){ local n=$1; shift; printf '    %-22s' "$n"; if ( "$@" ) > "$LOGS/$n.log" 2>&1; then echo ok; else echo "FAILED (see $LOGS/$n.log)"; tail -15 "$LOGS/$n.log" >&2; exit 1; fi; }
need(){ command -v "$1" >/dev/null 2>&1 || die "$1 not found. $2"; }
sha1(){ if command -v sha1sum >/dev/null; then sha1sum "$1" | cut -d' ' -f1; else shasum -a 1 "$1" | cut -d' ' -f1; fi; }
