/* The machine dependent library of the kernel: copying through the windows
 * of the system context, the messages, the memory of the machine, the end.
 */

#include "../kernel.h"
#include <signal.h>
#include <unistd.h>
#include <minix/com.h>
#include "../proc.h"
#include "stackette.h"

PUBLIC unsigned phys_page(a)
phys_bytes a;
{
  union physw u;

  u.l = a;
  return((u.w[1] << 4) | (u.w[0] >> 12));
}

PUBLIC phys_bytes phys_make(page, offset)
unsigned page, offset;
{
  union physw u;

  u.w[1] = page >> 4;
  u.w[0] = (page << 12) | offset;
  return(u.l);
}

/*===========================================================================*
 *				phys_copy_sys				     *
 *===========================================================================*/
PUBLIC void phys_copy_sys(src, dst, n)
phys_bytes src, dst, n;
{
/* Copy n bytes between physical addresses, a piece at a time through two
 * windows (data space pages 11 and 12). Only the system context may.
 */
  unsigned so, dof, chunk;
  unsigned *s, *d;
  unsigned char *sb, *db;
  unsigned i;

  while (n > 0) {
	so = (unsigned) src & (PAGE_SIZE - 1);
	dof = (unsigned) dst & (PAGE_SIZE - 1);
	chunk = PAGE_SIZE - (so > dof ? so : dof);
	if (n < chunk) chunk = (unsigned) n;
	SMMU(SP_DATA, WIN_SRC) = phys_page(src);
	SMMU(SP_DATA, WIN_DST) = phys_page(dst);
	if (((so | dof | chunk) & 1) == 0) {
		s = WINDOW(WIN_SRC) + (so >> 1);
		d = WINDOW(WIN_DST) + (dof >> 1);
		for (i = chunk >> 1; i > 0; i--) *d++ = *s++;
	} else {
		sb = BWINDOW(WIN_SRC) + so;
		db = BWINDOW(WIN_DST) + dof;
		for (i = chunk; i > 0; i--) *db++ = *sb++;
	}
	src += chunk;
	dst += chunk;
	n -= chunk;
  }
}

/*===========================================================================*
 *				phys_copy				     *
 *===========================================================================*/
PUBLIC void phys_copy(src, dst, n)
phys_bytes src, dst, n;
{
/* Copy between physical addresses: directly in the system context, through
 * a trap from a task.
 */
  struct kc_copy kc;

  if (in_kernel) {
	phys_copy_sys(src, dst, n);
  } else {
	kc.src = src;
	kc.dst = dst;
	kc.n = n;
	(void) kcall(KC_PHYS_COPY, &kc);
  }
}

/*===========================================================================*
 *				cp_mess					     *
 *===========================================================================*/
PUBLIC void cp_mess(src, src_clicks, src_offset, dst_clicks, dst_offset)
int src;			/* sender process */
phys_clicks src_clicks;		/* the data segments (the virtual 0) */
vir_bytes src_offset;
phys_clicks dst_clicks;
vir_bytes dst_offset;
{
/* Copy a message from the data space of one process to that of another and
 * fill in its source field. Only the system context calls this.
 */
  phys_bytes s = phys_make(src_clicks, 0) + src_offset;
  phys_bytes d = phys_make(dst_clicks, 0) + dst_offset;

  phys_copy_sys(s, d, (phys_bytes) sizeof(message));
  SMMU(SP_DATA, WIN_DST) = phys_page(d);
  ((message *) (WINDOW(WIN_DST) + (((unsigned) d & (PAGE_SIZE - 1)) >> 1)))
							->m_source = src;
}

/*===========================================================================*
 *				mem_init				     *
 *===========================================================================*/
PUBLIC void mem_init()
{
/* The memory MM manages is the RAM from FIRST_USER_PAGE on; the images of MM,
 * FS and INIT are at the start (main.c takes them off the list).
 */
  mem[0].base = FIRST_USER_PAGE;
  mem[0].size = RAM_PAGES - FIRST_USER_PAGE;
  tot_mem_size = RAM_PAGES;
}

/*===========================================================================*
 *				wreboot					     *
 *===========================================================================*/
PUBLIC void wreboot(how)
int how;
{
/* There is nowhere to reboot to: say so and stop. */
  static char msg[] = "\nMinix halted\n";
  char *p;

  for (p = msg; *p != 0; p++) UART_DATA = *p;
  lock();
  while (TRUE) {}
}
