/* The task of the ENC28J60 ethernet controller: it talks the protocol of the
 * ethernet tasks of Minix (dp8390.c) with inet. The messages are
 *
 *   DL_INIT     port, process, mode     the reply is DL_INIT_REPLY, with the
 *                                       port and the ethernet address
 *   DL_WRITE(V) port, process, count, address of the packet (iovec) of the
 *               process
 *   DL_READ(V)  port, process, count, address of the buffer (iovec) of the
 *               process
 *   DL_GETSTAT  port, process, address of an eth_stat
 *   DL_STOP     port
 *
 * and a DL_TASK_REPLY (port, process, count read, status | error << 16) to the
 * process that asked. A read that finds no packet stays pending: the reply
 * comes when a packet arrives (HARD_INT, the INT pin of the chip on the line 2
 * of the interrupt controller).
 *
 * The chip (DS39662) is on the SPI master at 0xf040: the store of DATA sends
 * a byte (st.b) or two (st, the low one first), the load of DATA waits for the
 * transfer and returns what came in; CONTROL bit 0 is CS_N (the times are kept
 * by the hardware). The buffer memory of 8 KiB is the ring of the receiver
 * (0 .. RX_END) and the transmit buffer (TX_START ...). The task is a plain
 * user context task: only the interrupt handler, which closes the line and
 * wakes the task up, runs in the system context.
 *
 * The workarounds of the errata sheet that matter here: ERXST is 0, ERXRDPT is
 * odd (the end of the ring or one before the next packet), the transmitter
 * is reset before every TXRTS.
 */

#include "../kernel.h"
#include <minix/com.h>
#include <minix/type.h>
#include <net/gen/ether.h>
#include <net/gen/eth_io.h>
#include "../proc.h"
#include "stackette.h"

#define SPI_DATA	PERIPH(0xf040)
#define SPI_DATA_B	(*(volatile unsigned char *) 0xf040)
#define SPI_CS		PERIPH(0xf042)
#define ENC_IRQ		2

/* A register is its address, its bank << 5 (4: all banks) and MAC for the
 * MAC and MII registers (a read has a dummy byte).
 */
#define BANK(b)		((b) << 5)
#define COMMON		BANK(4)
#define MAC		0x100
#define ERDPT		(BANK(0) | 0x00)
#define EWRPT		(BANK(0) | 0x02)
#define ETXST		(BANK(0) | 0x04)
#define ETXND		(BANK(0) | 0x06)
#define ERXST		(BANK(0) | 0x08)
#define ERXND		(BANK(0) | 0x0a)
#define ERXRDPT		(BANK(0) | 0x0c)
#define ERXFCON		(BANK(1) | 0x18)
#define EPKTCNT		(BANK(1) | 0x19)
#define MACON1		(BANK(2) | MAC | 0x00)
#define MACON3		(BANK(2) | MAC | 0x02)
#define MACON4		(BANK(2) | MAC | 0x03)
#define MABBIPG		(BANK(2) | MAC | 0x04)
#define MAIPGL		(BANK(2) | MAC | 0x06)
#define MAIPGH		(BANK(2) | MAC | 0x07)
#define MAMXFL		(BANK(2) | MAC | 0x0a)
#define MIREGADR	(BANK(2) | MAC | 0x14)
#define MIWRL		(BANK(2) | MAC | 0x16)
#define MIWRH		(BANK(2) | MAC | 0x17)
#define MAADR5		(BANK(3) | MAC | 0x00)
#define MAADR6		(BANK(3) | MAC | 0x01)
#define MAADR3		(BANK(3) | MAC | 0x02)
#define MAADR4		(BANK(3) | MAC | 0x03)
#define MAADR1		(BANK(3) | MAC | 0x04)
#define MAADR2		(BANK(3) | MAC | 0x05)
#define MISTAT		(BANK(3) | MAC | 0x0a)
#define EIE		(COMMON | 0x1b)
#define EIR		(COMMON | 0x1c)
#define ESTAT		(COMMON | 0x1d)
#define ECON2		(COMMON | 0x1e)
#define ECON1		(COMMON | 0x1f)

#define OP_WCR		0x40
#define OP_BFS		0x80
#define OP_BFC		0xa0
#define CMD_RBM		0x3a
#define CMD_WBM		0x7a
#define CMD_RESET	0xff

#define ECON1_TXRST	0x80
#define ECON1_RXEN	0x04
#define ECON1_TXRTS	0x08
#define ECON2_PKTDEC	0x40
#define EIE_INTIE	0x80
#define EIE_PKTIE	0x40
#define EIR_TXIF	0x08
#define EIR_TXERIF	0x02
#define ESTAT_CLKRDY	0x01
#define MISTAT_BUSY	0x01
#define PHCON2		0x10
#define PHCON2_HDLDIS	0x0100

#define RX_START	0x0000
#define RX_END		0x19ff		/* odd */
#define TX_START	0x1a00
#define BUF_BYTES	256		/* the bounce buffer, in bytes */
#define IOV_NR		16
#define POLLS		20000

PRIVATE ether_addr_t my_address = { { 0x02, 0x00, 0x00, 0x28, 0x60, 0x01 } };

PRIVATE int present;			/* the chip answers */
PRIVATE int cur_bank;
PRIVATE int irq_off;			/* the line is closed until we are done */
PRIVATE unsigned next_ptr;		/* the next packet in the ring */
PRIVATE int client;			/* the process of inet */
PRIVATE int read_pending;
PRIVATE iovec_t read_iov[IOV_NR];
PRIVATE int read_n;
PRIVATE int read_proc;
PRIVATE int flags;			/* the DL_*_REQ modes */
PRIVATE eth_stat_t stat;
PRIVATE unsigned bounce[BUF_BYTES / 2];
PRIVATE irq_hook_t enc_hook;

FORWARD _PROTOTYPE( int enc_hw_int, (irq_hook_t *hook)			);

/*===========================================================================*
 *				the SPI					     *
 *===========================================================================*/

PRIVATE void wr(op, reg, v)
int op, reg, v;
{
/* A command with one data byte: WCR, BFS or BFC. */
  int b = (reg >> 5) & 7;

  if (b < 4 && b != cur_bank) {
	cur_bank = b;
	wr(OP_BFC, ECON1, 3);
	wr(OP_BFS, ECON1, b);
  }
  SPI_CS = 1;
  SPI_DATA = (op | (reg & 0x1f)) | (v << 8);
  SPI_CS = 0;
}

PRIVATE unsigned rd(reg)
int reg;
{
/* RCR: a control register. */
  int b = (reg >> 5) & 7;
  unsigned v;

  if (b < 4 && b != cur_bank) {
	cur_bank = b;
	wr(OP_BFC, ECON1, 3);
	wr(OP_BFS, ECON1, b);
  }
  SPI_CS = 1;
  if (reg & MAC) {
	SPI_DATA = reg & 0x1f;		/* the command and the dummy byte */
	SPI_DATA_B = 0;
	v = SPI_DATA_B;
  } else {
	SPI_DATA = reg & 0x1f;		/* the command and the byte */
	v = SPI_DATA >> 8;
  }
  SPI_CS = 0;
  return(v & 0xff);
}

PRIVATE void wr16(reg, v)
int reg;
unsigned v;
{
/* A register pair, the low byte first. */
  wr(OP_WCR, reg, v & 0xff);
  wr(OP_WCR, reg + 1, v >> 8);
}

PRIVATE void rbm(p, n)
unsigned char *p;
unsigned n;
{
/* Read n bytes of the buffer memory (p is even, the pointer of the chip
 * counts up by itself).
 */
  unsigned *w = (unsigned *) p;
  unsigned x;

  SPI_CS = 1;
  SPI_DATA_B = CMD_RBM;
  for (; n >= 2; n -= 2) {
	SPI_DATA = 0;
	x = SPI_DATA;
	*w++ = x;
  }
  if (n) {
	SPI_DATA_B = 0;
	*(unsigned char *) w = SPI_DATA_B;
  }
  SPI_CS = 0;
}

PRIVATE void wbm(p, n)
unsigned char *p;
unsigned n;
{
/* Write n bytes to the buffer memory. */
  unsigned *w = (unsigned *) p;

  SPI_CS = 1;
  SPI_DATA_B = CMD_WBM;
  for (; n >= 2; n -= 2) SPI_DATA = *w++;
  if (n) SPI_DATA_B = *(unsigned char *) w;
  SPI_CS = 0;
}

PRIVATE void phy_wr(reg, v)
int reg;
unsigned v;
{
  int i;

  wr(OP_WCR, MIREGADR, reg);
  wr(OP_WCR, MIWRL, v & 0xff);
  wr(OP_WCR, MIWRH, v >> 8);
  for (i = 0; i < POLLS && (rd(MISTAT) & MISTAT_BUSY); i++) ;
}

/*===========================================================================*
 *				the chip				     *
 *===========================================================================*/

PRIVATE void rx_filter()
{
  unsigned f;

  f = 0xa1;			/* unicast to us, a good CRC, broadcast */
  if (flags & DL_PROMISC_REQ) f = 0x00;
  else if (flags & DL_MULTI_REQ) f |= 0x02;
  wr(OP_WCR, ERXFCON, f);
}

PRIVATE int chip_init()
{
/* Reset the chip and set it up; false if it is not there. */
  int i;

  SPI_CS = 0;
  SPI_CS = 1;
  SPI_DATA_B = CMD_RESET;
  SPI_CS = 0;
  cur_bank = 0;
  for (i = 0; i < POLLS && !(rd(ESTAT) & ESTAT_CLKRDY); i++) ;
  if (i == POLLS) return(FALSE);
  for (i = 0; i < 200; i++) (void) rd(ESTAT);	/* the errata: 300 us */

  wr16(ERXST, RX_START);
  wr16(ERXND, RX_END);
  wr16(ERXRDPT, RX_END);
  wr16(ERDPT, RX_START);
  wr16(ETXST, TX_START);
  next_ptr = RX_START;
  rx_filter();
  wr(OP_WCR, MACON1, 0x01);		/* MARXEN */
  wr(OP_WCR, MACON3, 0x32);		/* pad to 60, CRC, frame length check */
  wr(OP_WCR, MACON4, 0x40);		/* DEFER */
  wr(OP_WCR, MABBIPG, 0x12);
  wr(OP_WCR, MAIPGL, 0x12);
  wr(OP_WCR, MAIPGH, 0x0c);
  wr16(MAMXFL, ETH_MAX_PACK_SIZE_TAGGED);
  wr(OP_WCR, MAADR1, my_address.ea_addr[0]);
  wr(OP_WCR, MAADR2, my_address.ea_addr[1]);
  wr(OP_WCR, MAADR3, my_address.ea_addr[2]);
  wr(OP_WCR, MAADR4, my_address.ea_addr[3]);
  wr(OP_WCR, MAADR5, my_address.ea_addr[4]);
  wr(OP_WCR, MAADR6, my_address.ea_addr[5]);
  phy_wr(0x00, 0);			/* PHCON1: half duplex */
  phy_wr(PHCON2, PHCON2_HDLDIS);
  wr(OP_BFC, EIR, 0xff);
  wr(OP_WCR, EIE, EIE_INTIE | EIE_PKTIE);
  wr(OP_BFS, ECON1, ECON1_RXEN);
  return(TRUE);
}

PRIVATE void tx_wait()
{
  int i;

  for (i = 0; i < POLLS && (rd(ECON1) & ECON1_TXRTS); i++) ;
}

/*===========================================================================*
 *				the protocol				     *
 *===========================================================================*/

PRIVATE void reply(count, status)
int count;
int status;
{
  message r;

  r.m_type = DL_TASK_REPLY;
  r.DL_PORT = 0;
  r.DL_PROC = client;
  r.DL_COUNT = count;
  r.DL_STAT = status;			/* the error, OK, is in bits 16 and up */
  r.DL_CLCK = get_uptime();
  if (send(client, &r) != OK) panic("enc28j60: send failed", NO_NUM);
}

PRIVATE int get_iov(m, vectored, iov, proc)
message *m;
int vectored;
iovec_t *iov;
int *proc;
{
/* The vector of the request: its length, and the total size in the end. */
  int n;

  *proc = m->DL_PROC;
  if (vectored) {
	n = m->DL_COUNT;
	if (n > IOV_NR) n = IOV_NR;
	if (vir_copy(m->m_source, (vir_bytes) m->DL_ADDR, ENC28J60,
			(vir_bytes) iov, (vir_bytes) (n * sizeof(iovec_t))) != OK)
		panic("enc28j60: bad iovec", NO_NUM);
	return(n);
  }
  iov[0].iov_addr = (vir_bytes) m->DL_ADDR;
  iov[0].iov_size = m->DL_COUNT;
  return(1);
}

PRIVATE int iov_size(iov, n)
iovec_t *iov;
int n;
{
  int i, size = 0;

  for (i = 0; i < n; i++) size += iov[i].iov_size;
  return(size);
}

PRIVATE void nic2user(len, iov, n, proc)
unsigned len;
iovec_t *iov;
int n;
int proc;
{
/* Move len bytes of the packet (the pointer of the chip is there) to the
 * buffers of the process, in pieces through the bounce buffer.
 */
  unsigned chunk, addr, size;
  int i;

  for (i = 0; i < n && len > 0; i++) {
	addr = iov[i].iov_addr;
	size = iov[i].iov_size;
	while (size > 0 && len > 0) {
		chunk = BUF_BYTES;
		if (chunk > size) chunk = size;
		if (chunk > len) chunk = len;
		rbm((unsigned char *) bounce, chunk);
		if (vir_copy(ENC28J60, (vir_bytes) bounce, proc, addr,
							(vir_bytes) chunk) != OK)
			panic("enc28j60: copy failed", NO_NUM);
		addr += chunk;
		size -= chunk;
		len -= chunk;
	}
  }
}

PRIVATE void user2nic(iov, n, proc)
iovec_t *iov;
int n;
int proc;
{
  unsigned chunk, addr, size;
  int i;

  for (i = 0; i < n; i++) {
	addr = iov[i].iov_addr;
	size = iov[i].iov_size;
	while (size > 0) {
		chunk = BUF_BYTES;
		if (chunk > size) chunk = size;
		if (vir_copy(proc, addr, ENC28J60, (vir_bytes) bounce,
							(vir_bytes) chunk) != OK)
			panic("enc28j60: copy failed", NO_NUM);
		wbm((unsigned char *) bounce, chunk);
		addr += chunk;
		size -= chunk;
	}
  }
}

PRIVATE void do_write(m, vectored)
message *m;
int vectored;
{
  PRIVATE iovec_t iov[IOV_NR];		/* static: see vir_copy */
  int n, proc, size;

  client = m->DL_PROC;
  n = get_iov(m, vectored, iov, &proc);
  size = iov_size(iov, n);
  if (size < ETH_MIN_PACK_SIZE || size > ETH_MAX_PACK_SIZE)
	panic("enc28j60: invalid packet size", size);
  if (present) {
	tx_wait();
	wr(OP_BFS, ECON1, ECON1_TXRST);		/* the errata */
	wr(OP_BFC, ECON1, ECON1_TXRST);
	wr(OP_BFC, EIR, EIR_TXIF | EIR_TXERIF);
	wr16(EWRPT, TX_START);
	bounce[0] = 0;				/* no per packet options */
	wbm((unsigned char *) bounce, 1);
	user2nic(iov, n, proc);
	wr16(ETXND, TX_START + size);
	wr(OP_BFS, ECON1, ECON1_TXRTS);
	stat.ets_packetT++;
  }
  reply(0, DL_PACK_SEND);
}

PRIVATE void rearm()
{
/* Open the line again when the chip has no packet that waits. */
  if (irq_off && rd(EPKTCNT) == 0) {
	irq_off = FALSE;
	enable_irq(&enc_hook);
  }
}

PRIVATE int recv_one()
{
/* Hand the oldest packet of the ring to the pending read; false if there
 * is none.
 */
  unsigned char *h = (unsigned char *) bounce;
  unsigned next, len, st, rdpt;

  if (!present || !read_pending || rd(EPKTCNT) == 0) return(FALSE);
  wr16(ERDPT, next_ptr);
  rbm(h, 6);
  next = h[0] | h[1] << 8;
  len = (h[2] | h[3] << 8) - 4;			/* the CRC stays behind */
  st = h[4];
  if ((st & 0x80) && len <= ETH_MAX_PACK_SIZE_TAGGED) {
	nic2user(len, read_iov, read_n, read_proc);
	stat.ets_packetR++;
  } else {
	len = 0;
	stat.ets_recvErr++;
  }
  next_ptr = next;
  rdpt = next == RX_START ? RX_END : next - 1;
  wr16(ERXRDPT, rdpt);
  wr(OP_BFS, ECON2, ECON2_PKTDEC);
  read_pending = FALSE;
  reply(len, len > 0 ? DL_PACK_RECV : 0);
  return(TRUE);
}

PRIVATE void do_read(m, vectored)
message *m;
int vectored;
{
  int size;

  client = m->DL_PROC;
  if (read_pending) panic("enc28j60: read already in progress", NO_NUM);
  read_n = get_iov(m, vectored, read_iov, &read_proc);
  size = iov_size(read_iov, read_n);
  if (size < ETH_MAX_PACK_SIZE) panic("enc28j60: wrong packet size", size);
  read_pending = TRUE;
  if (!recv_one()) {
	reply(0, 0);
	return;
  }
  rearm();
}

PRIVATE void do_init(m)
message *m;
{
  message r;

  flags = m->DL_MODE;
  client = m->m_source;
  if (present) {
	rx_filter();
	wr(OP_BFS, ECON1, ECON1_RXEN);
  }
  r.m_type = DL_INIT_REPLY;
  r.m3_i1 = m->DL_PORT;
  r.m3_i2 = 1;
  *(ether_addr_t *) r.m3_ca1 = my_address;
  if (send(m->m_source, &r) != OK) panic("enc28j60: send failed", NO_NUM);
}

PRIVATE void do_getstat(m)
message *m;
{
  client = m->DL_PROC;
  if (vir_copy(ENC28J60, (vir_bytes) &stat, m->DL_PROC, (vir_bytes) m->DL_ADDR,
						(vir_bytes) sizeof(stat)) != OK)
	panic("enc28j60: bad stat address", NO_NUM);
  reply(0, 0);
}

PRIVATE int enc_hw_int(hook)
irq_hook_t *hook;
{
/* The chip asks for service: the line stays closed until the task has taken
 * the packets (INT is a level, up while EPKTCNT is not 0).
 */
  interrupt(ENC28J60);
  return(0);
}

PUBLIC void enc28j60_task()
{
  message m;

  present = chip_init();
  put_irq_handler(&enc_hook, ENC_IRQ, enc_hw_int);
  if (present) enable_irq(&enc_hook);

  while (TRUE) {
	if (receive(ANY, &m) != OK) panic("enc28j60: receive failed", NO_NUM);

	switch (m.m_type) {
	case DL_INIT:	do_init(&m);		break;
	case DL_WRITE:	do_write(&m, FALSE);	break;
	case DL_WRITEV:	do_write(&m, TRUE);	break;
	case DL_READ:	do_read(&m, FALSE);	break;
	case DL_READV:	do_read(&m, TRUE);	break;
	case DL_GETSTAT: do_getstat(&m);	break;
	case DL_STOP:	break;
	case HARD_INT:
		irq_off = TRUE;
		while (recv_one()) ;
		rearm();
		break;
	default:
		panic("enc28j60: illegal message", m.m_type);
	}
  }
}
