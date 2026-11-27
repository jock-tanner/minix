#!/bin/sh
# Builds the image with the ACK tools in it and compiles test/hello.c on the
# model with cc, which takes about 15 minutes of the model's time: cpp, prune,
# cemcom, em_opt, ncg, as and the linker, all running on stackette. The
# program must be byte for byte the one that the host linked (SAME).
#
#   ACK=<ack checkout> STACKETTE=<stackette repository> [MXW=..] [W=..] sh check.sh
set -e
here=$(cd "$(dirname "$0")" && pwd)
M=$(cd "$here/../.." && pwd)
: "${ACK:?}"
: "${STACKETTE:?set STACKETTE to the stackette repository (the model)}"
MXW=${MXW:-$M/tools-stackette/build}
W=${W:-$here/build}
rm -f "$MXW/root.stamp" "$MXW/root.img" "$MXW/cf.img"
make -C "$M/tools-stackette" ACK="$ACK/.obj/staging" W="$MXW" ROOT=cf EXTRA_ROOT="$W/root" -j8
STACKETTE=$STACKETTE python3 "$here/runmodel.py" "$MXW" \
	'cd /t && cc -O -o hello hello.c && cmp hello hello.ref && echo SAME && ./hello'
