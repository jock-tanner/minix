! _winmap(slot, page, n): trap 7 of the kernel, the pages page.. of the pool of
! the process show in the slots slot.. of its data window (page 0xffff: the
! own pages). The kernel reads the three arguments at [pfp+0], [pfp+2], [pfp+4]
! and pushes the result 0 or an error; the stub moves it into the deepest
! argument slot, as for send().

.sect .text
.sect .rom
.sect .data
.sect .bss
.sect .text

.define __winmap

__winmap:
	trap $7
	st [pfp+4], pop
	retn $2
