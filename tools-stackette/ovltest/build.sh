#!/bin/sh
# Builds ovltest, a program with a root and two overlays (ova.c, ovb.c), the
# test of the overlays: the calls root -> overlay -> overlay -> root, a pointer
# to a function of an overlay, a recursion across the overlays and a fork in
# an overlay. It prints a=1111 fp=1105 even=1 0 fork=42.
# S: the ACK install, L: the library of build.sh, M: the Minix tree
S=${S:-/tmp/ackwork/ack/.obj/staging}
L=${L:-/tmp/minixlib}
M=${M:-$(cd "$(dirname "$0")/../.." && pwd)}
here=$(cd "$(dirname "$0")" && pwd)
w=${W:-/tmp/ovltest.w}
rm -rf $w; mkdir -p $w/o $w/s; cd $w; export ACKDIR=$S
CC="$S/bin/ack -mstackette -O -D_MINIX -D_POSIX_SOURCE -I$M/include"
$CC -c.o -o o/main.o $here/main.c || exit 1
for f in ova ovb; do $CC -c.s -o s/$f.s $here/$f.c || exit 1; done
python3 $M/tools-stackette/ovlprep.py -o p -t p/ovl_thunks.s g0 s/ova.s + g1 s/ovb.s || exit 1
for f in p/*.s; do $S/bin/ack -mstackette -c.o -o o/$(basename $f .s).o $f || exit 1; done
$S/lib/ack/em_led -a0:2 -a1:2 -a2:2 -a3:2 -a4:2 -a5:2 -b1:0 -b4:0xc000 -b5:0xc000 -o ovltest.out \
  $L/crtso.o o/main.o o/ovl_thunks.o o/ova.o o/ovb.o $L/libc.a $L/libm.a \
  $S/share/ack/stackette/libem.a $S/share/ack/stackette/libfp.a $S/share/ack/stackette/libend.a || exit 1
python3 $M/tools-stackette/ack2minix.py ovltest.out -o ovltest --stack 0x2000
