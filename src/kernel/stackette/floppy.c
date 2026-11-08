/* There is no floppy: the task slot of FLOPPY (the layout of the task table is
 * the same everywhere) answers every request with ENODEV.
 */

#include "../kernel.h"
#include <minix/callnr.h>
#include <minix/com.h>

PUBLIC void floppy_task()
{
  message m;

  while (TRUE) {
	receive(ANY, &m);
	m.m_type = TASK_REPLY;
	m.REP_PROC_NR = m.PROC_NR;
	m.REP_STATUS = ENODEV;
	send(m.m_source, &m);
  }
}
