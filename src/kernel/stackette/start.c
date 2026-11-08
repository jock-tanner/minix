/* The start of Minix on the Stackette. The first code (kboot.s, boot.c) has
 * put the kernel's text and data in place; kmain() does what the boot monitor
 * of the PC does: load the images of MM, FS and INIT from the flash into the
 * RAM, build the table of their a.out headers that main() reads at `aout`,
 * and set the boot parameters. Then it calls main().
 */

#include "../kernel.h"
#include <stdlib.h>
#include <a.out.h>
#include "stackette.h"

/* The table of the flash (mkflash.py): `KS`, n, then eight words each. */
#define KS_MAGIC	0x534b
#define ENT_WORDS	8
#define MAX_IMAGES	8

extern unsigned table_page;	/* boot.c: the flash page of the table */
extern unsigned text_end;	/* boot.c: the end of the kernel's text */

PRIVATE char aout_table[(MAX_IMAGES + 1) * A_MINHDR];

/* The boot parameters: a series of "name=value" strings, ending in an empty
 * one. What MM and FS ask for with sys_getenv().
 */
PRIVATE char k_environ[] =
	"rootdev=ram\0"
	"ramimagedev=ram\0"
	"memory=0:0\0"
	"processor=0\0";

PUBLIC char *getenv(name)
_CONST char *name;
{
/* Get environment value - kernel version of getenv. */
  register _CONST char *namep;
  register char *envp;

  for (envp = k_environ; *envp != 0;) {
	for (namep = name; *namep != 0 && *namep == *envp; namep++, envp++)
		;
	if (*namep == '\0' && *envp == '=') return(envp + 1);
	while (*envp++ != 0)
		;
  }
  return(NIL_PTR);
}

PRIVATE void zero_pages(page, count)
unsigned page, count;
{
  unsigned *w;
  unsigned i;

  while (count-- > 0) {
	SMMU(SP_DATA, WIN_DST) = page++;
	w = WINDOW(WIN_DST);
	for (i = 0; i < PAGE_SIZE / 2; i++) w[i] = 0;
  }
  SMMU(SP_DATA, WIN_DST) = WIN_DST;
}

PRIVATE void header(n, text, data, bss, total, base)
int n;
unsigned text, data, bss, total, base;
{
  struct exec *e = (struct exec *) (aout_table + n * A_MINHDR);

  e->a_magic[0] = A_MAGIC0;
  e->a_magic[1] = A_MAGIC1;
  e->a_flags = A_SEP;
  e->a_cpu = A_STACKETTE;
  e->a_hdrlen = A_MINHDR;
  e->a_text = text;
  e->a_data = data;
  e->a_bss = bss;
  e->a_total = total;
  e->a_syms = (long) base << PAGE_SHIFT;	/* where it is loaded */
}

PUBLIC void kmain()
{
  unsigned *e;
  unsigned n, i, next, tpage, ntp, dpage, dlen, bss, total;

  in_kernel = TRUE;
  printf("\nStackette: Minix boot\n");

  /* The kernel: its text is executed in the flash. */
  header(0, (text_end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1), 0, 0,
				KDATA_PAGES * PAGE_SIZE, FLASH_PAGE);
  code_base = (phys_bytes) FLASH_PAGE << PAGE_SHIFT;
  data_base = 0;
  aout = vir2phys(aout_table);
  mon_params = vir2phys(k_environ);
  mon_parmsize = sizeof(k_environ);

  /* The servers and INIT: the text and then the data, one after the other
   * from the start of the free memory.
   */
  SMMU(SP_DATA, WIN_SRC) = FLASH_PAGE + table_page;
  e = WINDOW(WIN_SRC);
  if (e[0] != KS_MAGIC) panic("no table of images in the flash", NO_NUM);
  n = e[1];
  if (n > MAX_IMAGES) n = MAX_IMAGES;
  next = FIRST_USER_PAGE;
  for (i = 0; i < n; i++) {
	SMMU(SP_DATA, WIN_SRC) = FLASH_PAGE + table_page;
	e = WINDOW(WIN_SRC) + 2 + ENT_WORDS * i;
	tpage = e[0];
	ntp = e[1];
	dpage = e[2];
	dlen = e[3];
	bss = e[4];
	total = e[5];
	phys_copy_sys((phys_bytes) (FLASH_PAGE + tpage) << PAGE_SHIFT,
		(phys_bytes) next << PAGE_SHIFT,
		(phys_bytes) ntp << PAGE_SHIFT);
	zero_pages(next + ntp, total >> PAGE_SHIFT);
	phys_copy_sys((phys_bytes) (FLASH_PAGE + dpage) << PAGE_SHIFT,
		(phys_bytes) (next + ntp) << PAGE_SHIFT, (phys_bytes) dlen);
	header(1 + i, ntp << PAGE_SHIFT, dlen, bss, total, next);
	next += ntp + (total >> PAGE_SHIFT);
  }

  main();
}
