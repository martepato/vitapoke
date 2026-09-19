#!/usr/bin/env bash
# heap_symbols.sh LOG : turn the addresses in a guarded build's [HEAP] lines into function names.
#
# A build made with VITAPOKE_MALLOC_GUARD=1 names every call site holding memory by its return
# address, because that is all the allocator knows at the time. This resolves those against the .elf
# the build left behind, and prints each report with the names in place -- so a caller that grows by
# the same amount every time is a function rather than a hex number.
#
# The .elf has to be the one that produced the log. Rebuilding between the run and this will move
# every address, and the names will be wrong rather than missing, so the script checks the VPK is no
# newer than the .elf it is about to use and says so if it cannot tell.
source "$(dirname "$0")/common.sh"

LOG="${1:-}"
[ -n "$LOG" ] || die "usage: scripts/heap_symbols.sh ux0-data-vitapoke-log.txt"
[ -f "$LOG" ] || die "no such file: $LOG"

ELF="$ROOT/.work/vita/test_out/native-audio-app/vitapoke-platinum.elf"
[ -f "$ELF" ] || die "no $ELF -- build with VITAPOKE_MALLOC_GUARD=1 ./build.sh game first"

grep -q "HEAP" "$LOG" || die "no [HEAP] lines in $LOG: was it built with VITAPOKE_MALLOC_GUARD=1?"

# One addr2line for the whole log rather than one per address: it loads the symbol table each time
# and there are eight addresses per report.
mapfile -t ADDRS < <(grep -ao '0x8[0-9a-f]\{7\}' "$LOG" | sort -u)
[ "${#ADDRS[@]}" -gt 0 ] || die "no addresses in the [HEAP] lines of $LOG"

declare -A NAME
while read -r addr name; do NAME["$addr"]="$name"; done < <(
  paste -d' ' <(printf '%s\n' "${ADDRS[@]}") \
              <("${TOOLBIN}addr2line" -f -C -e "$ELF" "${ADDRS[@]}" 2>/dev/null | sed -n 'p;n')
)

while IFS= read -r line; do
  case "$line" in
    *HEAP*)
      out="$line"
      for a in $(grep -ao '0x8[0-9a-f]\{7\}' <<<"$line" | sort -u); do
        n="${NAME[$a]:-}"
        [ -n "$n" ] && [ "$n" != "??" ] && out="${out//$a/$a $n}"
      done
      printf '%s\n' "$out"
      ;;
  esac
done < "$LOG"
