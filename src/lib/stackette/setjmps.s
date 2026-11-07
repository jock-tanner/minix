! setjmp and longjmp are system calls: the kernel reads the return address and
! the caller's PFP from the return stack. The buffer of the kernel is four
! words at env->__pc (see include/setjmp.h and doc/kernel.md), so the stubs
! pass its address. `setjmp` and `_setjmp` must be the frame that the kernel
! sees (the one that is returned to), so they do their other work in calls
! that come back and then trap.

.sect .text
.sect .rom
.sect .data
.sect .bss
.sect .text

.define __kjmp
.extern ___setjmpsave

! int setjmp(jmp_buf env): saves the signal mask
.define _setjmp
_setjmp:
	ld push, $1
	ld push, [pfp+0]
	calla ___setjmpsave
	ld push, [pfp+0]
	add tos, $6
	st [pfp+0], pop
	trap $32
	st [pfp+0], pop
	ret

! int _setjmp(jmp_buf env): does not
.define __setjmp
__setjmp:
	ld push, $0
	ld push, [pfp+0]
	calla ___setjmpsave
	ld push, [pfp+0]
	add tos, $6
	st [pfp+0], pop
	trap $32
	st [pfp+0], pop
	ret

! int __kjmp(void *buf, int val): the longjmp of the kernel, buf is env->__pc
__kjmp:
	trap $33
	st [pfp+2], pop
	retn $1
