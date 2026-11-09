! ___sigreturn: the return address of a signal handler. The kernel (do_sendsig)
! built the frame the handler was called with: PFP points to it, the signal
! number is at [pfp+0], the code at [pfp+2] and the address of the sigcontext
! at [pfp+4]. Return to the kernel with sigreturn(scp), which does not come back.

.sect .text
.sect .rom
.sect .data
.sect .bss
.sect .text

.define ___sigreturn
.extern __sigreturn

___sigreturn:
	ld push, [pfp+4]
	calla __sigreturn
