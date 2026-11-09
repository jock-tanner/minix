#!/bin/sh
# Links a Minix process (separate I&D: the code at 0 in the code space, the rom,
# data and bss from 0 in the data space) and writes the a.out executable.
#
#   ACK=.../staging OUT=/tmp/minixlib sh mkproc.sh prog.aout obj.o... [-- --stack N]
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
export ACKDIR=$ACK
$ACK/lib/ack/em_led -a0:2 -a1:2 -a2:2 -a3:2 -b1:0 -o "$tmp" "$OUT/crtso.o" $objs \
	"$OUT/libsys.a" "$OUT/libc.a" "$OUT/libm.a" "$PLAT/libem.a" "$PLAT/libfp.a" "$PLAT/libend.a"
python3 "$TOOLS/ack2minix.py" "$tmp" -o "$target" $extra
