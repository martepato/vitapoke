#!/usr/bin/env bash
# prereqs.sh [TOOL...] : check the host tools the build needs and offer to install any that are missing.
# Callers pass the tools their step actually runs: `setup` needs only git/make/curl/tar, while a game
# build also needs python3, patch and rsync. Never runs sudo without asking first (set VITAPOKE_ASSUME_YES=1 to skip the
# question, e.g. in a non-interactive setup). Exit 0 when everything is present.
source "$(dirname "$0")/common.sh"
TOOLS="${*:-git python3 make patch rsync curl tar}"
missing(){ local m="" t; for t in $TOOLS; do command -v "$t" >/dev/null 2>&1 || m="$m $t"; done; echo "${m# }"; }

M=$(missing); [ -z "$M" ] && exit 0

if [ "$(uname -s)" = Darwin ]; then
  die "missing: $M. Install Apple's command line tools with: xcode-select --install  (they include all of these), then run the build again."
fi

PM=""; for c in apt-get dnf pacman zypper; do command -v "$c" >/dev/null 2>&1 && { PM=$c; break; }; done
[ -n "$PM" ] || die "missing: $M. Install them with your package manager, then run the build again."

pkg_for(){ case "$PM:$1" in pacman:python3) echo python;; *) echo "$1";; esac; }
PKGS=""; for t in $M; do PKGS="$PKGS $(pkg_for "$t")"; done; PKGS="${PKGS# }"
case "$PM" in
  apt-get) CMD="apt-get install -y $PKGS";;
  dnf)     CMD="dnf install -y $PKGS";;
  pacman)  CMD="pacman -S --needed --noconfirm $PKGS";;
  zypper)  CMD="zypper install -y $PKGS";;
esac
SUDO=""; [ "$(id -u)" = 0 ] || SUDO="sudo "

echo "Missing tools: $M"
echo "To install them: ${SUDO}${CMD}"
if [ "${VITAPOKE_ASSUME_YES:-0}" = 1 ]; then ans=y
elif [ -t 0 ]; then printf 'Install them now? [y/N] '; read -r ans
else die "not an interactive terminal: run the command above, then run the build again (or set VITAPOKE_ASSUME_YES=1)"; fi
case "$ans" in y|Y|yes|YES) ;; *) die "run the command above, then run the build again";; esac

# shellcheck disable=SC2086  # intentional word splitting: this is the command line printed above
${SUDO}${CMD} || die "package install failed. Run the command above yourself, then run the build again."
M=$(missing); [ -z "$M" ] || die "still missing after the install: $M"
log "Installed: $PKGS"
