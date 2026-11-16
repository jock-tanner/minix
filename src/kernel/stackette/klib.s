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

! wcopy(src, dst, n): copy n bytes, n even and not 0, from src to dst with one
! string instruction. src is an address of the stack space (the source is
! popped, PSP is made to point at it) and dst one of the data space; the
! caller maps the same page at both. The next word after the block is not
! read, but the push that puts PSP back lands on the last word of the block:
! the caller writes that word again.

.define _wcopy

.sect .bss
wsave:
	.space 2
.sect .text

_wcopy:
	ld.push [pfp+2]
	add.tos [pfp+4]
	st.pop mbp
	ld.push [pfp+4]
	neg tos
	st.pop midx
	ld.push sp
	st.pop [wsave]
	ld.push [pfp+0]
	st.pop sp
	st.pop [mbp+midx++]*
	ld.push [wsave]
	st.pop sp
	retn $3
