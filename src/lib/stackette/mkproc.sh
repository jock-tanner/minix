#!/bin/sh
# Links a Minix process (separate I&D: the code at 0 in the code space, the rom,
# data and bss from 0 in the data space) and writes the a.out executable.
#
#   ACK=.../staging OUT=/tmp/minixlib [OVL=n] sh mkproc.sh prog.aout obj.o... [-- --stack N]
#
# OVL=n: the program has n overlays (tools-stackette/ovlprep.py): their sections
# are .ovl0 .. .ovl(n-1), sections 5 and up (4 is .end), all at the window, 0xc000.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
: "${ACK:?}"
OUT=${OUT:-$HERE/out}
TOOLS=$(cd "$HERE/../../../tools-stackette" && pwd)
PLAT=$ACK/share/ack/stackette
target=$1; shift
objs=
extra=
while [ $# -gt 0 ]; do
	if [ "$1" = -- ]; then shift; extra="$*"; break; fi
	objs="$objs $1"; shift
done
tmp=$target.out
ovlopt=
i=0
while [ $i -lt "${OVL:-0}" ]; do
	ovlopt="$ovlopt -a$((5 + i)):2 -b$((5 + i)):0xc000"; i=$((i + 1))
done
export ACKDIR=$ACK
$ACK/lib/ack/em_led -a0:2 -a1:2 -a2:2 -a3:2 $ovlopt -b1:0 -o "$tmp" "$OUT/crtso.o" $objs \
	"$OUT/libsys.a" "$OUT/libc.a" "$OUT/libm.a" "$PLAT/libfp.a" "$PLAT/libem.a" "$PLAT/libend.a"
python3 "$TOOLS/ack2minix.py" "$tmp" -o "$target" $extra
