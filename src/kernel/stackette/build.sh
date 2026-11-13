#!/bin/sh
# Builds the Minix kernel for the Stackette and the flash image:
#   ACK=.../staging OUT=/tmp/minixlib W=/tmp/minixk sh build.sh [process.out...]
# The processes (MM, FS, INIT: linked with mkproc.sh's em_led line) follow the
# kernel in the image in that order.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
: "${ACK:?}"
OUT=${OUT:-/tmp/minixlib}
W=${W:-/tmp/minixk}
KDIR=$HERE/..
TOOLS=$HERE/../../../tools-stackette
PLAT=$ACK/share/ack/stackette
export ACKDIR=$ACK
mkdir -p "$W"
cd "$W"
CC="$ACK/bin/ack -mstackette -O $KDEFS -D_MINIX -D_POSIX_SOURCE -I$HERE/../../../include -I$KDIR -I$KDIR/.."
objs=
for f in $HERE/boot $KDIR/main $KDIR/proc $KDIR/system $KDIR/clock $KDIR/memory $KDIR/driver \
		$KDIR/misc $KDIR/tty $KDIR/pty $KDIR/drvlib $KDIR/dmp $KDIR/table $KDIR/printer \
		$HERE/floppy $HERE/enc28j60 $HERE/ide_wini $HERE/start $HERE/mpx $HERE/klib $HERE/irq $HERE/uart; do
	b=$(basename $f)
	$CC -c.o -o $b.o $f.c 2>$b.err || { cat $b.err; exit 1; }
	objs="$objs $b.o"
done
for f in kboot klib; do
	$ACK/bin/ack -mstackette -c.o -o s_$f.o $HERE/$f.s
done
$ACK/lib/ack/em_led -a0:2 -a1:2 -a2:2 -a3:2 -b1:0x1000 -o kernel.out \
	s_kboot.o $objs s_klib.o "$OUT/libsys.a" "$OUT/libc.a" "$PLAT/libem.a" "$PLAT/libend.a"
python3 $TOOLS/mkflash.py -k kernel.out -o flash.bin --stack 0x2000 ${ROOTFS:+--rootfs $ROOTFS} "$@"
