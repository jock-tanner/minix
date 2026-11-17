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

! insw(buf, words) and outsw(buf, words): the data register of the IDE host
! (0xf080) to and from buf, in words (REP INSW and REP OUTSW). A string
! instruction goes through the port and the parameter stack, a second one
! through the stack and the buffer; the stack takes 2 * words bytes meanwhile.
! The first word read is the deepest one, hence the pre-decrement for the buffer.

.define _insw, _outsw

_insw:
	ld.push [pfp+2]
	shl tos, $1
	st.pop midx
	ld.push [0xf080]*
	ld.push [pfp+2]
	shl tos, $1
	st.pop midx
	ld.push [pfp+0]
	st.pop mbp
	st.pop [mbp+--midx]*
	retn $2

_outsw:
	ld.push [pfp+2]
	shl tos, $1
	st.pop midx
	ld.push [pfp+0]
	st.pop mbp
	ld.push [mbp+--midx]*
	ld.push [pfp+2]
	shl tos, $1
	st.pop midx
	st.pop [0xf080]*
	retn $2

! fillw(ptr, value, words): words words at ptr get the value (one string
! instruction). seqw(ptr, first, words): the words at ptr get first, first + 1
! and so on, as the page numbers of a map do.

.define _fillw, _seqw

_fillw:
	ld.push [pfp+0]
	add.tos [pfp+4]
	add.tos [pfp+4]
	st.pop mbp
	ld.push [pfp+4]
	add.tos [pfp+4]
	neg tos
	st.pop midx
	ld.push [pfp+2]
	st [mbp+midx++]*, tos
	st.pop null
	retn $3

_seqw:
	ld.push [pfp+0]
	add.tos [pfp+4]
	add.tos [pfp+4]
	st.pop mbp
	ld.push [pfp+4]
	add.tos [pfp+4]
	neg tos
	st.pop midx
	ld.push [pfp+2]
	ld.push midx
	cmp pop, $0
	skip ne
	bra seqw_done
seqw_loop:
	st [mbp+midx++], tos
	add tos, $1
	ld.push midx
	cmp pop, $0
	skip eq
	bra seqw_loop
seqw_done:
	st.pop null
	retn $3

! copyw(dst, src, words): a copy of words, in ascending order (peripheral
! registers too), by two string instructions through the stack.

.define _copyw

_copyw:
	ld.push [pfp+4]
	add.tos [pfp+4]
	st.pop midx
	ld.push [pfp+2]
	st.pop mbp
	ld.push [mbp+--midx]*
	ld.push [pfp+4]
	add.tos [pfp+4]
	neg tos
	st.pop midx
	ld.push [pfp+0]
	add.tos [pfp+4]
	add.tos [pfp+4]
	st.pop mbp
	st.pop [mbp+midx++]*
	retn $3
