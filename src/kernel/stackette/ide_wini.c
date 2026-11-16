/* The disk task of the Stackette: an ATA device (a CompactFlash card in True
 * IDE mode, or a disk) on the IDE host at 0xf080 (doc: ide_pio.py). It has the
 * interface of the other disk drivers of Minix (driver.c), so the partitions
 * of the MBR (drvlib.c: the primary and the extended partitions, and the
 * subpartitions of a Minix partition) come with it: /dev/hd0 is the disk,
 * /dev/hd1 ... /dev/hd4 its partitions, /dev/hd1a ... /dev/hd1d the
 * subpartitions of the first one, as on the PC.
 *
 * The card is polled, with PIO and 28-bit LBA addresses, one drive (the
 * master). The transfers are done a sector at a time through a buffer of the
 * task, which is in the data space of the kernel: the buffers of the callers
 * are reached with vir_copy(). The interrupt line of the card is not used.
 *
 * The boot parameters: c0=ide in the environment makes this the task of
 * /dev/c0, and idepio=4 asks the card for PIO mode 4 (120 ns per cycle,
 * instead of the 600 ns of the mode 0), which needs a short cable.
 */

#include "../kernel.h"
#include "../driver.h"
#include "../drvlib.h"
#include "stackette.h"
#include <stdlib.h>

#define IDE(n)		PERIPH(0xf080 + 2 * (n))
#define R_DATA		0
#define R_FEATURES	1
#define R_COUNT		2
#define R_SECTOR	3
#define R_CYL_LO	4
#define R_CYL_HI	5
#define R_LDH		6
#define R_STATUS	7
#define R_COMMAND	7
#define R_SETUP		8	/* the timing of the host: clocks of 20 ns */
#define R_STROBE	9
#define R_RECOVER	10
#define R_HOST		11
#define R_ALT_STATUS	14
#define R_CONTROL	14

#define STATUS_BSY	0x80
#define STATUS_RDY	0x40
#define STATUS_DF	0x20
#define STATUS_DRQ	0x08
#define STATUS_ERR	0x01
#define CONTROL_SRST	0x04
#define CONTROL_NIEN	0x02
#define LDH_LBA		0xe0	/* LBA, the master */

#define CMD_READ	0x20
#define CMD_WRITE	0x30
#define CMD_IDENTIFY	0xec
#define CMD_SET_FEATURES 0xef
#define FEAT_SET_MODE	0x03
#define MODE_PIO_4	0x0c

#define ERR		(-1)
#define MAX_SECS	127	/* a command: not more than a 16-bit count of bytes */
#define MAX_ERRORS	4
#define TIMEOUT		(5 * HZ)	/* the card takes its time to get ready */
#define SUB_PER_DRIVE	(NR_PARTITIONS * NR_PARTITIONS)

/* The disk. All of it is static: see vir_copy(). */
PRIVATE struct wini {
  int state;			/* 0: not identified, 1: ready, -1: absent */
  unsigned long sectors;
  unsigned open_ct;
  struct device part[DEV_PER_DRIVE];
  struct device subpart[SUB_PER_DRIVE];
} wini;

PRIVATE struct device *w_dv;
PRIVATE int w_task;			/* my task number */
PRIVATE unsigned sec[SECTOR_SIZE / 2];	/* the buffer of a sector */
PRIVATE char model[41];
PRIVATE clock_t w_start;

FORWARD _PROTOTYPE( int w_do_open, (struct driver *dp, message *m_ptr) );
FORWARD _PROTOTYPE( int w_do_close, (struct driver *dp, message *m_ptr) );
FORWARD _PROTOTYPE( struct device *w_prepare, (int device) );
FORWARD _PROTOTYPE( char *w_name, (void) );
FORWARD _PROTOTYPE( int w_transfer, (int proc_nr, int opcode, off_t position,
					iovec_t *iov, unsigned nr_req) );
FORWARD _PROTOTYPE( void w_geometry, (struct partition *entry) );
FORWARD _PROTOTYPE( int w_identify, (void) );
FORWARD _PROTOTYPE( int w_reset, (void) );
FORWARD _PROTOTYPE( int w_wait, (int mask, int value) );
FORWARD _PROTOTYPE( int w_command, (int cmd, unsigned count,
						unsigned long lba) );
FORWARD _PROTOTYPE( int w_chunk, (int proc_nr, int opcode, unsigned long lba,
						unsigned count, vir_bytes addr) );
FORWARD _PROTOTYPE( void w_pio_mode, (void) );
FORWARD _PROTOTYPE( void w_delay, (unsigned ticks) );

PRIVATE struct driver w_dtab = {
  w_name,		/* current device's name */
  w_do_open,		/* open or mount request, initialize device */
  w_do_close,		/* release device */
  do_diocntl,		/* get or set a partition's geometry */
  w_prepare,		/* prepare for I/O on a given minor device */
  w_transfer,		/* do the I/O */
  nop_cleanup,		/* nothing to clean up */
  w_geometry,		/* tell the geometry of the disk */
};


PUBLIC void ide_winchester_task()
{
  w_task = proc_number(proc_ptr);
  driver_task(&w_dtab);
}


PRIVATE char *w_name()
{
  return "ide0";
}


/* Wait for the status to be value under mask (polling the alternate status,
 * which does not touch the interrupt request): returns the status, or -1 after
 * the timeout.
 */
PRIVATE int w_wait(mask, value)
int mask, value;
{
  int st;

  w_start = get_uptime();
  for (;;) {
	st = IDE(R_ALT_STATUS) & 0xff;
	if ((st & mask) == value) return(st);
	if (get_uptime() - w_start > TIMEOUT) return(-1);
  }
}


/* Wait some ticks of the clock. */
PRIVATE void w_delay(ticks)
unsigned ticks;
{
  clock_t t;

  t = get_uptime();
  while (get_uptime() - t <= ticks) {}
}


/* The software reset: SRST for a few milliseconds, then wait for the card. */
PRIVATE int w_reset()
{
  IDE(R_CONTROL) = CONTROL_SRST | CONTROL_NIEN;
  w_delay(2);
  IDE(R_CONTROL) = CONTROL_NIEN;
  w_delay(2);
  IDE(R_LDH) = LDH_LBA;
  return w_wait(STATUS_BSY | STATUS_RDY, STATUS_RDY) < 0 ? ERR : OK;
}


/* Start a command with count sectors from lba. */
PRIVATE int w_command(cmd, count, lba)
int cmd;
unsigned count;
unsigned long lba;
{
  if (w_wait(STATUS_BSY, 0) < 0) return(ERR);
  IDE(R_LDH) = LDH_LBA | (unsigned) ((lba >> 24) & 0x0f);
  if (w_wait(STATUS_BSY | STATUS_RDY, STATUS_RDY) < 0) return(ERR);
  IDE(R_FEATURES) = 0;
  IDE(R_COUNT) = count;
  IDE(R_SECTOR) = (unsigned) (lba & 0xff);
  IDE(R_CYL_LO) = (unsigned) ((lba >> 8) & 0xff);
  IDE(R_CYL_HI) = (unsigned) ((lba >> 16) & 0xff);
  IDE(R_COMMAND) = cmd;
  return(OK);
}


/* IDENTIFY DEVICE: the size and the name of the card. */
PRIVATE int w_identify()
{
  int i, st;
  unsigned w;

  if (w_reset() != OK) return(ERR);
  if (w_command(CMD_IDENTIFY, 0, 0L) != OK) return(ERR);
  if ((st = w_wait(STATUS_BSY | STATUS_DRQ, STATUS_DRQ)) < 0) return(ERR);
  if (st & STATUS_ERR) return(ERR);
  insw(sec, SECTOR_SIZE / 2);
  if (w_wait(STATUS_BSY, 0) < 0) return(ERR);

  if (!(sec[49] & 0x0200)) {
	printf("%s: no LBA\n", w_name());
	return(ERR);
  }
  wini.sectors = ((unsigned long) sec[61] << 16) | sec[60];
  for (i = 0; i < 20; i++) {		/* the characters of a word swapped */
	w = sec[27 + i];
	model[2 * i] = w >> 8;
	model[2 * i + 1] = w & 0xff;
  }
  model[40] = 0;
  for (i = 39; i > 0 && model[i] == ' '; i--) model[i] = 0;
  wini.part[0].dv_base = mul64u(0L, SECTOR_SIZE);
  wini.part[0].dv_size = mul64u(wini.sectors, SECTOR_SIZE);
  w_pio_mode();
  printf("%s: %s, %u MB\n", w_name(), model,
					(unsigned) (wini.sectors >> 11));
  return(OK);
}


/* idepio=4 in the boot parameters: PIO mode 4 for the card and the host. */
PRIVATE void w_pio_mode()
{
  char *p;

  if ((p = getenv("idepio")) == NULL || *p != '4') return;
  if ((sec[64] & 2) == 0) return;	/* the card does not do it */
  if (w_wait(STATUS_BSY, 0) < 0) return;
  IDE(R_LDH) = LDH_LBA;
  IDE(R_FEATURES) = FEAT_SET_MODE;
  IDE(R_COUNT) = MODE_PIO_4;
  IDE(R_COMMAND) = CMD_SET_FEATURES;
  if (w_wait(STATUS_BSY, 0) < 0) return;
  IDE(R_SETUP) = 3;		/* 25 ns, 70 ns, 120 ns */
  IDE(R_STROBE) = 5;
  IDE(R_RECOVER) = 0;
}


PRIVATE int w_do_open(dp, m_ptr)
struct driver *dp;
message *m_ptr;
{
/* Initialize the card on the first open, and read the partition tables. */

  if (w_prepare(m_ptr->DEVICE) == NIL_DEV) return(ENXIO);
  if (wini.state == 0) {
	if (w_identify() != OK) {
		printf("%s: no card\n", w_name());
		wini.state = -1;
		return(ENXIO);
	}
	wini.state = 1;
  }
  if (wini.state < 0) return(ENXIO);
  if (wini.open_ct == 0) {
	partition(&w_dtab, 0, P_PRIMARY);
	wini.open_ct++;
  }
  return(OK);
}


PRIVATE int w_do_close(dp, m_ptr)
struct driver *dp;
message *m_ptr;
{
  return(OK);
}


PRIVATE struct device *w_prepare(device)
int device;
{
  if (device < DEV_PER_DRIVE) {			/* d0, d0p[0-3] */
	w_dv = &wini.part[device];
  } else
  if ((unsigned) (device -= MINOR_d0p0s0) < SUB_PER_DRIVE) {
	w_dv = &wini.subpart[device];		/* d0p[0-3]s[0-3] */
  } else {
	return(NIL_DEV);
  }
  return(w_dv);
}


/* One command: count sectors from lba to or from the process, a sector at a
 * time through the buffer of the task.
 */
PRIVATE int w_chunk(proc_nr, opcode, lba, count, addr)
int proc_nr, opcode;
unsigned long lba;
unsigned count;
vir_bytes addr;
{
  int st, i;
  unsigned *p;

  if (w_command(opcode == DEV_SCATTER ? CMD_WRITE : CMD_READ, count, lba)
								!= OK)
	return(ERR);
  while (count > 0) {
	if ((st = w_wait(STATUS_BSY, 0)) < 0) return(ERR);
	if (st & (STATUS_ERR | STATUS_DF)) return(ERR);
	if (!(st & STATUS_DRQ)) return(ERR);
	if (opcode == DEV_GATHER) {
		p = sec;
		insw(sec, SECTOR_SIZE / 2);
		if (vir_copy(w_task, (vir_bytes) sec, proc_nr, addr,
					(vir_bytes) SECTOR_SIZE) != OK)
			return(EFAULT);
	} else {
		if (vir_copy(proc_nr, addr, w_task, (vir_bytes) sec,
					(vir_bytes) SECTOR_SIZE) != OK)
			return(EFAULT);
		p = sec;
		outsw(sec, SECTOR_SIZE / 2);
	}
	addr += SECTOR_SIZE;
	count--;
  }
  /* the end of the command (a write is not done before) */
  if ((st = w_wait(STATUS_BSY, 0)) < 0) return(ERR);
  if (st & (STATUS_ERR | STATUS_DF)) return(ERR);
  return(OK);
}


PRIVATE int w_transfer(proc_nr, opcode, position, iov, nr_req)
int proc_nr;			/* process doing the request */
int opcode;			/* DEV_GATHER or DEV_SCATTER */
off_t position;			/* offset on device to read or write */
iovec_t *iov;			/* pointer to read or write request vector */
unsigned nr_req;		/* length of request vector */
{
  unsigned long dv_size = cv64ul(w_dv->dv_size);
  unsigned long block;
  unsigned nbytes, count;
  int r, errors;

  if ((position & SECTOR_MASK) != 0) return(EINVAL);

  while (nr_req > 0) {
	nbytes = iov->iov_size;
	if ((nbytes & SECTOR_MASK) != 0) return(EINVAL);
	if (position >= dv_size) return(OK);		/* at EOF */
	if (position + nbytes > dv_size) nbytes = dv_size - position;
	count = nbytes >> SECTOR_SHIFT;
	if (count > MAX_SECS) count = MAX_SECS;
	block = div64u(add64ul(w_dv->dv_base, position), SECTOR_SIZE);

	for (errors = 0; ; ) {
		r = w_chunk(proc_nr, opcode, block, count, iov->iov_addr);
		if (r == OK || r == EFAULT) break;
		if (++errors == MAX_ERRORS) break;
		w_reset();
	}
	if (r != OK) return(r == EFAULT ? r : EIO);

	nbytes = count << SECTOR_SHIFT;
	position += nbytes;
	iov->iov_addr += nbytes;
	if ((iov->iov_size -= nbytes) == 0) { iov++; nr_req--; }
  }
  return(OK);
}


PRIVATE void w_geometry(entry)
struct partition *entry;
{
/* Made-up numbers like the BIOS of the PC gives: 16 heads, 63 sectors. */
  unsigned long cyl = wini.sectors / (16 * 63);

  entry->cylinders = cyl > 0xffff ? 0xffff : (unsigned) cyl;
  entry->heads = 16;
  entry->sectors = 63;
}
