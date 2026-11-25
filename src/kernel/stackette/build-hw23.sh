#!/bin/sh
# Builds the hw23 flash image: ACK=.../staging OUT=/tmp/minixlib W=/tmp/hw23 sh build-hw23.sh
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
: "${ACK:?}"
OUT=${OUT:-/tmp/minixlib}
W=${W:-/tmp/hw23}
TOOLS=$HERE/../../../tools-stackette
PLAT=$ACK/share/ack/stackette
export ACKDIR=$ACK
mkdir -p "$W"
cd "$W"
CC="$ACK/bin/ack -mstackette -O -D_MINIX -D_POSIX_SOURCE -I$HERE/../../../include"
for f in boot hw23; do $CC -c.o -o $f.o $HERE/$f.c; done
$ACK/bin/ack -mstackette -c.o -o kboot.o $HERE/kboot.s
$ACK/lib/ack/em_led -a0:2 -a1:2 -a2:2 -a3:2 -b1:0x1000 -o kernel.out kboot.o boot.o hw23.o \
	"$PLAT/libem.a" "$PLAT/libend.a"
procs=
for u in user1 user2; do
	$CC -c.o -o $u.o $HERE/hw23_$u.c
	$ACK/bin/ack -mstackette -c.o -o ustubs.o $HERE/ustubs.s
	$ACK/lib/ack/em_led -a0:2 -a1:2 -a2:2 -a3:2 -b1:0 -o $u.out "$OUT/crtso.o" $u.o ustubs.o \
		"$OUT/libc.a" "$OUT/libsys.a" "$PLAT/libem.a" "$PLAT/libfp.a" "$PLAT/libend.a"
	procs="$procs $u.out"
done
python3 $TOOLS/mkflash.py -k kernel.out -o flash.bin --stack 0x1000 $procs
