/* The first C code of the kernel: it runs in the first page of the code, the
 * only one that is mapped at the start, and calls nothing but itself. */

#define SMMU ((volatile unsigned *) 0xf100)
#define DATA_MMU 48
#define FLASH 0x2000
#define WINDOW ((unsigned *) 0xb000)

unsigned table_page;		/* the flash page of the table of the processes */

void bootc(et, ed, eb)
unsigned et, ed, eb;
{
  register unsigned i, n;
  register unsigned *s, *d;
  unsigned first, pages;

  /* the code pages of the kernel are the flash */
  n = et >> 12;
  for (i = 0; i <= n; i++) SMMU[i] = FLASH + i;

  /* the rom and the data: the image is in the flash from the next page, it
   * goes to the data space from 0x1000 (RAM page 1), a page at a time through
   * the window 11 */
  first = n + 1;
  if ((et & 0xfff) == 0) first = n;
  pages = (ed - 0x1000 + 0xfff) >> 12;
  d = (unsigned *) 0x1000;
  for (n = 0; n < pages; n++) {
	SMMU[DATA_MMU + 11] = FLASH + first + n;
	s = WINDOW;
	for (i = 0; i < 2048; i++) *d++ = *s++;
  }
  SMMU[DATA_MMU + 11] = 11;

  /* the bss */
  for (d = (unsigned *) ed; d < (unsigned *) eb; d++) *d = 0;

  table_page = first + pages;
}
