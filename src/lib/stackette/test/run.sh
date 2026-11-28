#!/bin/sh
# Builds a test program against the Minix library and runs it on the Python model
# of the CPU, with test/harness.c standing in for the kernel (it implements WRITE,
# EXIT and BRK of the library's _sendrec).
#
#   ACK=... OUT=... MODEL=/path/to/stackette-repo sh run.sh t30.c
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
LIB=$(cd "$HERE/../.." && pwd)
INC=$(cd "$LIB/../../include" && pwd)
: "${ACK:?}" "${MODEL:?}"
TOOLS=$(cd "$LIB/../../tools-stackette" && pwd)
OUT=${OUT:-$LIB/out}
PLAT=$ACK/share/ack/stackette
export ACKDIR=$ACK
f=$1; b=$(basename "$f" .c)
mkdir -p "$OUT/test" && cd "$OUT/test"
F="-mstackette -O -D_MINIX -D_POSIX_SOURCE -I$INC"
$ACK/bin/ack $F -c.o "$HERE/$f"
$ACK/bin/ack $F -c.o "$HERE/harness.c"
$ACK/bin/ack -mstackette -c.o "$HERE/start.s"
$ACK/lib/ack/em_led -a0:2 -a1:2 -a2:2 -a3:2 -b2:0xb000 -o "$b.out" "$PLAT/boot.o" start.o "$OUT/crtso.o" "$b.o" harness.o \
	"$OUT/libc.a" "$OUT/libsys.a" "$OUT/libm.a" "$PLAT/libfp.a" "$PLAT/libem.a" "$PLAT/libend.a"
python3 "$TOOLS/ack2bin.py" "$b.out" -o "$b.bin"
PYTHONPATH=$MODEL python3 "$TOOLS/run_model.py" "$b.bin" --steps 50000000 2>/dev/null || true
