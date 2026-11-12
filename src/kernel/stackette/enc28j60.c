/* The task of the ENC28J60 ethernet controller: it talks the protocol of the
 * ethernet tasks of Minix (dp8390.c) with inet. The messages are
 *
 *   DL_INIT     port, process, mode     the reply is DL_INIT_REPLY, with the
 *                                       port and the ethernet address
 *   DL_WRITE    port, process, count, address of the packet of the process
 *   DL_READ     port, process, count, address of the buffer of the process
 *   DL_GETSTAT  port, process, address of an eth_stat
 *   DL_STOP     port
 *
 * and a DL_TASK_REPLY (port, process, count read, status | error << 16) to the
 * process that asked. There is no chip behind it yet: it accepts and drops
 * what is written and never has anything to read.
 */

#include "../kernel.h"
#include <minix/com.h>
#include <net/gen/ether.h>

PRIVATE ether_addr_t my_address = { { 0x02, 0x00, 0x00, 0x28, 0x60, 0x01 } };

PRIVATE void reply(m, count, status)
message *m;
int count;
int status;
{
  message r;

  r.m_type = DL_TASK_REPLY;
  r.DL_PORT = m->DL_PORT;
  r.DL_PROC = m->DL_PROC;
  r.DL_COUNT = count;
  r.DL_STAT = status;			/* the error, OK, is in bits 16 and up */
  r.DL_CLCK = get_uptime();
  if (send(m->m_source, &r) != OK) panic("enc28j60: send failed", NO_NUM);
}

PUBLIC void enc28j60_task()
{
  message m, r;

  while (TRUE) {
	if (receive(ANY, &m) != OK) panic("enc28j60: receive failed", NO_NUM);

	switch (m.m_type) {
	case DL_INIT:
		r.m_type = DL_INIT_REPLY;
		r.m3_i1 = m.DL_PORT;
		r.m3_i2 = 1;
		*(ether_addr_t *) r.m3_ca1 = my_address;
		if (send(m.m_source, &r) != OK)
			panic("enc28j60: send failed", NO_NUM);
		break;
	case DL_WRITE:
	case DL_WRITEV:
		reply(&m, 0, DL_PACK_SEND);
		break;
	case DL_READ:
	case DL_READV:
	case DL_GETSTAT:
		reply(&m, 0, 0);
		break;
	case DL_STOP:
		break;
	case HARD_INT:
		break;
	default:
		panic("enc28j60: illegal message", m.m_type);
	}
  }
}
