! The start of a process: the stack holds argc, then the argv array, a NULL,
! the environment (the image that exec builds), and PFP = PSP at the first
! word. main(argc, argv, envp) takes its arguments with argc on top.

.sect .text
.sect .rom
.sect .data
.sect .bss
.sect .text

.define crtso, begtext, __penviron
.extern _main, _exit, __penvp

begtext:
crtso:
	ld push, [pfp+0]
	add.tos tos
	add.tos $2
	lea push, [pfp+2]
	add.nos tos
	ld push, nos
	ld push, [__penviron]
	st mbp, pop
	ld push, $0
	st midx, pop
	st [mbp+midx], pop
	ld push, [pfp+0]
	calla _main
	calla _exit
crtso_halt:
	bra crtso_halt

.sect .data
__penviron:
	.data2 __penvp
.sect .bss
.comm __penvp, 2
