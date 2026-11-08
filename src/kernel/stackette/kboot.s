! The start of the system context (the kernel image): address 0 of the flash.
! The CPU starts it with PSP = 0xf000, RSP = 0 and PFP = PSP, so a function can
! be called at once. The ID register is 0 only after a power-up; a fault of the
! system context enters this address again, with another ID.
!
! bootc(endtext, enddata, endbss), in boot.c, maps the code pages and copies
! the rom and data image (flash page after the text, at the data space address
! 0x1000) and clears the bss; then kmain() runs.

.sect .text
.sect .rom
.sect .data
.sect .bss
.sect .text

.define begtext, _ctxrun
.extern endtext, enddata, endbss, _bootc, _kmain

begtext:
	ld push, [0xf02e]
	sub tos, $0
	st null, pop
	skip eq
	bra fault
	ld push, #endbss
	ld push, #enddata
	ld push, #endtext
	calla _bootc
	calla _kmain
halt:
	bra halt

! a fault of the system context: write ! and stop
fault:
	ld push, $33
	st [0xf000], pop
	bra halt

! ctxrun(): the user context runs. The system context carries on here when the
! user traps, faults or is interrupted; the cause is in the ID register.
_ctxrun:
	reti
	retn $0
