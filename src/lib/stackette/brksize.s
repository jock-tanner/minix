.sect .text
.sect .rom
.sect .data
.sect .bss
.define __brksize
.extern endbss
.sect .data
__brksize:
	.data2 endbss
