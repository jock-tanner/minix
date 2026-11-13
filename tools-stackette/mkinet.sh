#!/bin/sh
# Builds inet with overlays (ovlprep.py, doc in src/lib/stackette/README). The
# groups below are the overlays, the other files of inet are linked in the root.
# The objects of all the files are compiled before, in $W/inet and
# $W/inet_generic (the Makefile does it); this script compiles the files of the
# groups again, to assembly, for ovlprep.py.
#
#   ACK=.../staging OUT=libdir M=minix-tree W=workdir sh mkinet.sh
#
# The result is $W/inetovl/inet.aout; /bin/inet, with /etc/inet.conf
# "eth0 ENC28J 0 { default; };" and the node /dev/ip: c 7 0.
set -e
: "${ACK:?}" "${OUT:?}" "${M:?}" "${W:?}"
export ACKDIR=$ACK
D=$W/inetovl
rm -rf "$D"; mkdir -p "$D/s" "$D/o"
CC="$ACK/bin/ack -mstackette -O -D_MINIX -D_POSIX_SOURCE -I$M/include -I$M/src/inet -I$M/src/inet/generic -I$M/src"
G0="tcp tcp_lib"
G1="tcp_recv tcp_send"
G2="udp ip_ioctl psip"
args=
skip=
n=0
for g in G0 G1 G2; do
	eval list=\$$g
	args="$args ${args:+ + }g$n"
	for f in $list; do
		$CC -c.s -o "$D/s/$f.s" "$M/src/inet/generic/$f.c" 2>/dev/null
		args="$args $D/s/$f.s"; skip="$skip $f"
	done
	n=$((n + 1))
done
python3 "$M/tools-stackette/ovlprep.py" -o "$D/p" -t "$D/p/ovl_thunks.s" $args
for f in "$D"/p/*.s; do
	$ACK/bin/ack -mstackette -c.o -o "$D/o/$(basename "$f" .s).o" "$f"
done
roots=
for f in "$W"/inet/*.o "$W"/inet_generic/*.o; do
	b=$(basename "$f" .o)
	case " $skip " in *" $b "*) continue;; esac
	roots="$roots $f"
done
groups=
for f in "$D"/o/*.o; do
	case $f in *ovl_thunks.o) ;; *) groups="$groups $f";; esac
done
ACK=$ACK OUT=$OUT OVL=$n sh "$M/src/lib/stackette/mkproc.sh" "$D/inet.aout" \
	$roots "$D/o/ovl_thunks.o" $groups -- --stack 0x2000 --heap 0x2000
