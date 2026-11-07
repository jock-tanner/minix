! The message passing calls of the kernel: _send(dest, ptr), _receive(src, ptr)
! and _sendrec(srcdest, ptr). The kernel reads the arguments at [pfp+0] and
! [pfp+2] and pushes the result; the stub moves it into the deepest argument
! slot, as the epilogue of any function does (see doc/kernel.md).

.sect .text
.sect .rom
.sect .data
.sect .bss
.sect .text

.define __send, __receive, __sendrec

__send:
	trap $1
	st [pfp+2], pop
	retn $1

__receive:
	trap $2
	st [pfp+2], pop
	retn $1

__sendrec:
	trap $3
	st [pfp+2], pop
	retn $1
