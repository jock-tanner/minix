/* The first C code of the kernel: it runs in the first page of the code, the
 * only one that is mapped at the start, and calls nothing but itself. */

#define SMMU ((volatile unsigned *) 0xf100)
#define DATA_MMU 48
#define FLASH 0x2000
#define WINDOW ((unsigned *) 0xb000)
#define DWINDOW ((unsigned *) 0xc000)

/* the RAM pages the text is copied to (stackette.h: SHADOW_PAGE, SHADOW_PAGES) */
#define SHADOW 0x40
#define SHADOW_MAX 16

unsigned table_page;		/* the flash page of the table of the processes */
unsigned text_end;		/* the end of the kernel's text */


void bootc(et, ed, eb)
unsigned et, ed, eb;
{
  register unsigned i, n;
  register unsigned *s, *d;
  unsigned first, pages;
  register volatile unsigned *p, *q, *r;

  /* the code pages of the kernel are the flash */
  /* no shifts: the helper routines are not in the first page */
  n = 0;
  for (i = et; i >= 0x1000; i -= 0x1000) n++;
  p = SMMU;
  for (i = 0; i <= n; i++) *p++ = FLASH + i;

  /* the rom and the data: the image is in the flash from the next page, it
   * goes to the data space from 0x1000 (RAM page 1), a page at a time through
   * the window 11 */
  first = n + 1;
  if ((et & 0xfff) == 0) first = n;
  pages = 0;
  for (i = ed - 0x1000; i > 0; i -= 0x1000) {
	pages++;
	if (i < 0x1000) break;
  }
  q = SMMU + DATA_MMU + 11;
  d = (unsigned *) 0x1000;
  for (n = 0; n < pages; n++) {
	*q = FLASH + first + n;
	s = WINDOW;
	for (i = 0; i < 2048; i++) *d++ = *s++;
  }
  *q = 11;

  /* The text: the flash is slow (64 to 192 clocks a word), so the code runs
   * from a copy in the RAM when it fits. The copy is the same bytes, so
   * switching the page that runs this code is invisible. */
  if (first <= SHADOW_MAX) {
	r = SMMU + DATA_MMU + 12;
	for (n = 0; n < first; n++) {
		*q = FLASH + n;
		*r = SHADOW + n;
		s = WINDOW;
		d = DWINDOW;
		for (i = 0; i < 2048; i++) *d++ = *s++;
		/* the first page is this code: from here on it runs from the
		 * RAM, which makes the rest of the copy faster */
		if (n == 0) *SMMU = SHADOW;
	}
	*q = 11;
	*r = 12;
	p = SMMU + 1;
	for (n = 1; n < first; n++) *p++ = SHADOW + n;
  }

  /* the bss */
  for (d = (unsigned *) ed; d < (unsigned *) eb; d++) *d = 0;

  table_page = first + pages;
  text_end = et;		/* (after the bss is cleared) */
}
