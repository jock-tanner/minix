.sect .text
.sect .rom
.sect .data
.sect .bss
.sect .text
.extern crtso
.define __m_a_i_n
__m_a_i_n:
	ld push, $0
	ld push, $0
	ld push, #name
	ld push, $1
	calla crtso
	retn $0
.sect .data
name:
	.data1 0x61, 0
