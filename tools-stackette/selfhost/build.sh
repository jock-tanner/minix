#!/bin/sh
# Builds the tools that make ACK run on stackette (cpp, prune, cemcom, em_opt,
# ncg, as, em_led, the ack driver) with ACK itself, and the tree to put in the
# root image. See README.md.
#
#   ACK=<ack checkout, built> [MXW=<build dir of the Makefile>] [W=<work dir>] \
#     sh build.sh [step...]
#
# steps: source cross libs tools install (all of them by default)
set -e

here=$(cd "$(dirname "$0")" && pwd)
M=$(cd "$here/../.." && pwd)
: "${ACK:?set ACK to the ack checkout, with its default platform built}"
ACK=$(cd "$ACK" && pwd)
ACKDIR=${ACKDIR:-$ACK/.obj/staging}
MXW=${MXW:-$M/tools-stackette/build}
OUT=$MXW/lib
W=${W:-$here/build}
mkdir -p "$W"
W=$(cd "$W" && pwd)
X=$W/ack
if [ "$W" = "$MXW" ]; then
	echo "build.sh: W and MXW must be different directories: this script removes and rewrites lib, bin and root in W" >&2
	exit 1
fi
MKPROC=$M/src/lib/stackette/mkproc.sh
OVLPREP=$M/tools-stackette/ovlprep.py
export ACKDIR

for f in crtso.o libsys.a libc.a libm.a; do
	[ -f "$OUT/$f" ] || { echo "build.sh: $OUT/$f is missing: build the Minix libraries first (make in tools-stackette)" >&2; exit 1; }
done

NINJA="ninja -f .obj/build.ninja"
CEMCOM=.obj/lang/cem/cemcom.ansi/+cemcom
CPP=.obj/lang/cem/cpp.ansi/+cpp.ansi
OPT=.obj/util/opt/+em_opt
NCG=.obj/plat/stackette/+ncg
AS=.obj/plat/stackette/+as
DRIVER=.obj/util/ack/+ack
PARAMS=.obj/lang/cem/cemcom.ansi/+parameters/parameters.h
BINS="$CEMCOM/cemcom $CPP/cpp.ansi $OPT/em_opt $NCG/ncg $AS/as $DRIVER/ack"

say() {
	echo "== $*"
}

ackc() {
	"$ACKDIR/bin/ack" -mstackette -O -c.o "$@"
}

# a copy of the ack sources, with the changes for the target, and the host
# build of the tools: it generates the sources of the cross build
step_source() {
	say source
	rm -rf "$X"
	mkdir -p "$X"
	git -C "$ACK" archive HEAD | tar x -C "$X"
	(
		cd "$X"
		patch -s -p1 <"$here/ack-alloc.patch"
		patch -s -p1 <"$here/led-target.patch"
		make .obj/build.ninja
		$NINJA $BINS
	)
}

# the objects that link a target, looking into its libraries (but not into the
# tools that generate sources: they are for the host)
objsof() {
	cmd=$($NINJA -t commands -s "$1")
	case $cmd in
	'sh '*.sh) cmd=$(cat "${cmd#sh }");;
	esac
	echo "$cmd" | tr ' ' '\n' | while read -r w; do
		case $w in
		*.o) echo "$w";;
		*.a) [ "$w" = "$1" ] || objsof "$w";;
		esac
	done
}

# the objects of the tools, compiled for stackette
step_cross() {
	say cross
	rm -rf "$W/inc" "$W/bin" "$W/s"
	mkdir -p "$W/inc" "$W/bin" "$W/s"
	cp "$here/inc/stdint.h" "$W/inc/"
	cat >"$W/inc/string.h" <<EOF
/* the Minix string.h, and strdup, which the ack sources use */
#include "$M/include/string.h"
#ifndef SELFHOST_STRDUP
#define SELFHOST_STRDUP
char *strdup(const char *s);
#endif
EOF
	cp "$here/cc" "$W/bin/cc"
	chmod 755 "$W/bin/cc"
	cd "$X"
	# the parameters of cemcom for a small machine: SmallPars, the comments made fit for ack
	cp "$here/cemcom-parameters.h" "$X/$PARAMS"
	for b in $BINS; do
		objsof "$b"
	done | sort -u | grep -v 'syssystem\.o$\|maketempfile\.o$' >"$W/objs.txt"
	# the compile commands of ninja, run by hand: asking ninja to rebuild the
	# objects would also rebuild the host tools that share them
	while read -r o; do
		$NINJA -t commands -s "$o"
	done <"$W/objs.txt" >"$W/compile.txt"
	rm -f $(cat "$W/objs.txt")
	export SH_INC=$W/inc SH_S=$W/s MINIX_INC=$M/include PATH=$W/bin:$PATH
	xargs -d '\n' -P "${JOBS:-4}" -I '{}' sh -c '{}' <"$W/compile.txt"
}

# the objects of each tool, and the library of what the tools share
own() {
	# own TOOL DIR: copies the objects of the tool to $W/TOOL/o
	rm -rf "$W/$1/o"
	mkdir -p "$W/$1/o"
	grep -F "$2/" "$W/objs.txt" | while read -r f; do
		[ ! -f "$W/$1/o/$(basename "$f")" ] || { echo "build.sh: two objects $(basename "$f") in $1" >&2; exit 1; }
		cp "$X/$f" "$W/$1/o/"
	done
}

step_libs() {
	say libs
	own cemcom "$CEMCOM"
	own cpp "$CPP"
	own opt "$OPT"
	own ncg "$NCG"
	own as "$AS"
	own ack "$DRIVER"
	rm -rf "$W/lib" "$W/extra"
	mkdir -p "$W/lib/o" "$W/extra"
	grep -v -F -e "$CEMCOM/" -e "$CPP/" -e "$OPT/" -e "$NCG/" -e "$AS/" -e "$DRIVER/" "$W/objs.txt" |
	while read -r f; do
		[ ! -f "$W/lib/o/$(basename "$f")" ] || { echo "build.sh: two library objects $(basename "$f")" >&2; exit 1; }
		cp "$X/$f" "$W/lib/o/"
	done
	(cd "$W/lib/o" && "$ACKDIR/bin/aal" rc ../cemlibs.a *.o)
	ackc -I"$W/inc" -I"$M/include" -o "$W/extra/extra.o" "$here/src/extra.c"
	ackc -I"$W/inc" -I"$M/include" -o "$W/extra/maketempfile-stub.o" "$here/src/maketempfile-stub.c"
	ackc -I"$W/inc" -I"$M/include" -I"$X/.obj/modules/src/system/+system_hdr" -I"$X/.obj/modules/src/string/+string_hdr" \
		-I"$X/.obj/modules/src/data/+data_hdr" -o "$W/extra/maketempfile.o" "$here/src/maketempfile.c"
	ackc -I"$W/inc" -I"$M/include" -I"$X/.obj/modules/src/system/+system_hdr" -o "$W/extra/syssystem.o" "$here/src/syssystem.c"
}

# links a tool: TOOL OUT STACK HEAP "EXTRA OBJECTS" [OVERLAY...], an overlay being
# the list of the modules in it
link() {
	tool=$1
	out=$2
	stack=$3
	heap=$4
	extra=$5
	shift 5
	d=$W/$tool
	n=$#
	if [ $n -eq 0 ]; then
		ACK=$ACKDIR OUT=$OUT sh "$MKPROC" "$d/$out" "$d"/o/*.o $extra "$W/lib/cemlibs.a" -- --stack "$stack" --heap "$heap"
		return
	fi
	rm -rf "$d/p" "$d/o2"
	mkdir -p "$d/p" "$d/o2"
	args=
	mods=
	i=0
	for g in "$@"; do
		args="$args g$i"
		for f in $g; do
			args="$args $W/s/$tool/$f.s"
			mods="$mods $f"
		done
		args="$args +"
		i=$((i + 1))
	done
	args=${args% +}
	python3 "$OVLPREP" -o "$d/p" -t "$d/p/ovl_thunks.s" $args
	for f in "$d"/p/*.s; do
		"$ACKDIR/bin/ack" -mstackette -c.o -o "$d/o2/$(basename "$f" .s).o" "$f"
	done
	roots=
	for f in "$d"/o/*.o; do
		[ -f "$d/o2/$(basename "$f")" ] || roots="$roots $f"
	done
	ovs=
	for f in $mods; do
		ovs="$ovs $d/o2/$f.o"
	done
	ACK=$ACKDIR OUT=$OUT OVL=$n sh "$MKPROC" "$d/$out" $roots $extra "$d/o2/ovl_thunks.o" $ovs "$W/lib/cemlibs.a" -- --stack "$stack" --heap "$heap"
}

step_tools() {
	say tools
	E=$W/extra
	link cemcom cemcom.aout 0xc00 0x8f00 "$E/extra.o" \
		'LLlex expression ch3 ch3bin' \
		'eval arith cstoper fltcstoper conversion' \
		'declar declarator decspecs struct type proto' \
		'ival code statement program switch' \
		'idf expr ch3mon field options blocks stack util LLmessage symbol2str'
	link cpp cpp.aout 0x1000 0x4000 "$E/extra.o" \
		'domacro replace' \
		'LLlex preprocess'
	link opt em_opt.aout 0x1000 0x4000 "$E/extra.o $E/maketempfile-stub.o"
	link ncg ncg.aout 0x1000 0x4000 "$E/extra.o $E/maketempfile-stub.o" \
		'codegen' \
		'compute fillem' \
		'subr gencode'
	link as as.aout 0x1000 0x8000 "$E/extra.o $E/maketempfile-stub.o" \
		'bison' \
		'comm5 comm7 comm6'
	link ack ack.aout 0x1000 0x8000 "$E/extra.o $E/syssystem.o $E/maketempfile.o"

	# the linker is not cross-built from the ninja objects: it has its own configuration
	L=$W/led
	rm -rf "$L"
	mkdir -p "$L/src" "$L/o"
	cp "$X"/util/led/*.c "$X"/util/led/*.h "$L/src/"
	cat >"$L/src/local.h" <<EOF
#define VERSION 3
#define ACKM "stackette"
#define SYS_5
EOF
	# the object module with small write buffers: three of them cost 4.6 KB of the data space
	mkdir -p "$L/object"
	cp "$X"/modules/src/object/*.c "$X"/modules/src/object/*.h "$L/object/"
	sed -i 's/^#define WBUFSIZ BUFSIZ$/#define WBUFSIZ 256/' "$L/object/obj.h"
	grep -q '^#define WBUFSIZ 256$' "$L/object/obj.h"
	INC="-I$L/src -DNORETURN= -I$X/h -I$X/modules/h -I$X/modules/src/data -I$L/object -I$X/modules/src/string -I$X/modules/src/system -I$X/modules/src/alloc -I$X/.obj/h/+h_hdr"
	for f in "$L"/src/*.c "$L"/object/*.c "$X"/modules/src/string/*.c; do
		ackc $INC -o "$L/o/$(basename "$f" .c).o" "$f"
	done
	ACK=$ACKDIR OUT=$OUT sh "$MKPROC" "$L/led.aout" "$L"/o/*.o -- --stack 0x800 --heap 0xbc00

	# the small tools
	for t in prune ack2minix; do
		mkdir -p "$W/$t"
		ackc -I"$M/include" -o "$W/$t/$t.o" "$here/src/$t.c"
	done
	ACK=$ACKDIR OUT=$OUT sh "$MKPROC" "$W/prune/prune.aout" "$W/prune/prune.o" -- --stack 0x800 --heap 0x3000
	ACK=$ACKDIR OUT=$OUT sh "$MKPROC" "$W/ack2minix/ack2minix.aout" "$W/ack2minix/ack2minix.o" -- --stack 0x800 --heap 0x1000
}

# the tree for the root image: the ack tree in /opt/pkg/ack, /bin/ack, /bin/cc,
# /bin/prune and the Minix headers in /inc
step_install() {
	say install
	R=$W/root
	D=$R/opt/pkg/ack
	rm -rf "$R"
	mkdir -p "$R/bin" "$D/share/ack/descr" "$D/share/ack/stackette" "$D/share/ack/minix" "$D/lib/ack/stackette"
	cp "$here/tree/descr" "$D/share/ack/descr/stackette"
	cp "$ACKDIR/share/ack/descr/fe" "$D/share/ack/descr/fe"
	cp -R "$ACKDIR/share/ack/include" "$D/share/ack/include"
	cp -R "$ACKDIR/share/ack/stackette/include" "$D/share/ack/stackette/include"
	cp "$ACKDIR/share/ack/stackette/libem.a" "$ACKDIR/share/ack/stackette/libfp.a" "$ACKDIR/share/ack/stackette/libend.a" "$D/share/ack/stackette/"
	cp "$OUT/crtso.o" "$OUT/libsys.a" "$OUT/libc.a" "$OUT/libm.a" "$D/share/ack/minix/"
	cp "$W/cpp/cpp.aout" "$D/lib/ack/cpp.real"
	cp "$here/tree/cpp.ansi" "$D/lib/ack/cpp.ansi"
	cp "$W/cemcom/cemcom.aout" "$D/lib/ack/em_cemcom.ansi"
	cp "$W/opt/em_opt.aout" "$D/lib/ack/em_opt"
	cp "$W/led/led.aout" "$D/lib/ack/em_led"
	cp "$here/tree/mled" "$D/lib/ack/mled"
	cp "$W/ack2minix/ack2minix.aout" "$D/lib/ack/ack2minix"
	cp "$W/ncg/ncg.aout" "$D/lib/ack/stackette/ncg"
	cp "$W/as/as.aout" "$D/lib/ack/stackette/as"
	cp "$W/ack/ack.aout" "$R/bin/ack"
	cp "$here/tree/cc" "$R/bin/cc"
	cp "$W/prune/prune.aout" "$R/bin/prune"
	cp -R "$M/include" "$R/inc"
	# a test: hello.c, and hello.ref, which is hello.c linked on the host
	mkdir -p "$R/t" "$W/hello"
	cp "$here/test/hello.c" "$R/t/hello.c"
	ackc -D_MINIX -D_POSIX_SOURCE -I"$M/include" -o "$W/hello/hello.o" "$here/test/hello.c" 2>/dev/null
	ACK=$ACKDIR OUT=$OUT sh "$MKPROC" "$R/t/hello.ref" "$W/hello/hello.o" -- --stack 0x1000 --heap 0x2000
	chmod 755 "$D"/lib/ack/* "$D"/lib/ack/stackette/* "$R"/bin/*
	echo "root tree: $R (make EXTRA_ROOT=$R in tools-stackette)"
}

steps=${*:-source cross libs tools install}
for s in $steps; do
	"step_$s"
done
