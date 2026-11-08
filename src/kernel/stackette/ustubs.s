! The system calls of hw23: the kernel reads the argument at [pfp+0] and pushes
! the result, which goes to the place of the argument.

.sect .text
.sect .rom
.sect .data
.sect .bss
.sect .text

.define _kputc, _kyield, _kexit

_kputc:
	trap $8
	st [pfp+0], pop
	retn $0

_kyield:
	trap $10
	retn $0

_kexit:
	trap $9
halt:
	bra halt
