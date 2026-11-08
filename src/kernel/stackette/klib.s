! kcall(code, args): a task asks the system context for a service (trap 4).
! The kernel pushes the result, which goes to the place of the first argument
! like the result of any function.

.sect .text
.sect .rom
.sect .data
.sect .bss
.sect .text

.define _kcall

_kcall:
	trap $4
	st [pfp+2], pop
	retn $1
