#include "syslib.h"

PUBLIC int sys_winset(proc, pool, len, wbase, wn, keep)
int proc;			/* process that gets the pool and the window */
phys_clicks pool;		/* first page of the pool */
unsigned len;			/* its length in pages */
unsigned wbase;			/* first page of the window in the data space */
unsigned wn;			/* its length in pages */
int keep;			/* the slots keep what they show (a fork) */
{
/* The memory manager gives a process a pool of pages and a window to look at
 * them (stackette). Tell the kernel.
 */

  message m;

  m.m1_i1 = proc;
  m.m1_i2 = pool;
  m.m1_i3 = len;
  m.m1_p1 = (char *) wbase;
  m.m1_p2 = (char *) wn;
  m.m1_p3 = (char *) keep;
  return(_taskcall(SYSTASK, SYS_WINSET, &m));
}
