#!/bin/sh
# Builds the Minix C library with the Stackette ACK: libc.a, libsys.a, libm.a and crtso.o.
#
#   ACK=/path/to/ack/.obj/staging  OUT=/path/to/output  sh build.sh
#
# ACK is the staging directory of an ACK build that has the stackette platform
# (bin/ack, share/ack/stackette, lib/ack). The objects and the three archives
# go to $OUT (default: ./out next to this script's lib directory).
# Minix's own Makefiles need the Minix cc; this script compiles every file
# separately, the way the Makefiles do (-O -D_MINIX -D_POSIX_SOURCE).
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
LIB=$(cd "$HERE/.." && pwd)
INC=$(cd "$LIB/../../include" && pwd)
: "${ACK:?set ACK to the staging directory of the ACK build}"
OUT=${OUT:-$LIB/out}
OPT=${OPT:--O}
PLAT=$ACK/share/ack/stackette
export ACKDIR=$ACK
CC="$ACK/bin/ack -mstackette $OPT -D_MINIX -D_POSIX_SOURCE -I$INC -I$LIB/.."
mkdir -p "$OUT"
fail=0
cc_dir() {	# dir: every .c file of $LIB/dir
	d=$1; mkdir -p "$OUT/$d"
	for f in "$LIB/$d"/*.c; do
		b=$(basename "$f" .c)
		if ! (cd "$OUT/$d" && $CC -I"$LIB/$d" -c.o "$f" >/dev/null 2>"$b.err"); then
			echo "FAILED: $d/$b.c (see $OUT/$d/$b.err)"; fail=1
		else rm -f "$OUT/$d/$b.err"; fi
	done
}
as_dir() {	# dir: every .s file of $LIB/dir
	d=$1; mkdir -p "$OUT/$d"
	for f in "$LIB/$d"/*.s; do
		b=$(basename "$f" .s)
		(cd "$OUT/$d" && $ACK/bin/ack -mstackette -c.o "$f" >/dev/null 2>"$b.err") || { echo "FAILED: $d/$b.s"; fail=1; }
		rm -f "$OUT/$d/$b.err"
	done
}
for d in ansi posix other stdio syslib math fphook stackette; do cc_dir $d; done
# the one's complement checksum of the network code
mkdir -p "$OUT/ip"
(cd "$OUT/ip" && $CC -I"$LIB/ip" -c.o "$LIB/ip/oneC_sum.c") || { echo "FAILED: ip/oneC_sum.c"; fail=1; }
as_dir syscall
as_dir stackette

# libc: everything but the dummy float routines (fltpr, strtod of fphook), the
# kernel-side libraries and the startup code; frexp and modf come from ACK's libc
rm -f "$OUT"/libc.a "$OUT"/libsys.a "$OUT"/libm.a
mkdir -p "$OUT/ackc"
(cd "$OUT/ackc" && $ACK/bin/aal x "$PLAT/libc.a" frexp.o modf.o)
$ACK/bin/aal rc "$OUT/libc.a" "$OUT"/ansi/*.o "$OUT"/posix/*.o "$OUT"/other/*.o \
	"$OUT"/stdio/*.o "$OUT"/syscall/*.o "$OUT"/fphook/fphook.o \
	"$OUT"/stackette/sendrec.o "$OUT"/stackette/brksize.o \
	"$OUT"/stackette/hton.o "$OUT"/ip/oneC_sum.o "$OUT"/stackette/setjmp.o "$OUT"/stackette/setjmps.o "$OUT"/stackette/sigreturn.o \
	"$OUT"/ackc/frexp.o "$OUT"/ackc/modf.o
$ACK/bin/aal rc "$OUT/libsys.a" "$OUT"/syslib/*.o "$OUT"/stackette/int64.o
$ACK/bin/aal rc "$OUT/libm.a" "$OUT"/math/*.o
cp "$OUT/stackette/crtso.o" "$OUT/crtso.o"
[ $fail = 0 ] && echo "built: $OUT/libc.a libsys.a libm.a crtso.o"
exit $fail
