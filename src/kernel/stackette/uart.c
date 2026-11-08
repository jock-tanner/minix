/* The console of the Stackette: the UART. It is the tty of Minix's console.c
 * and keyboard.c in one: output goes straight to the transmitter (which
 * stalls when it is busy), input comes in by the receiver's interrupt line and
 * is queued for the tty task.
 */

#include "../kernel.h"
#include <termios.h>
#include <signal.h>
#include <minix/callnr.h>
#include <minix/com.h>
#include "../tty.h"
#include "../proc.h"
#include "stackette.h"

#define IN_BYTES	64

PRIVATE unsigned char ibuf[IN_BYTES];
PRIVATE unsigned char *ihead = ibuf, *itail = ibuf;
PRIVATE int icount;

FORWARD _PROTOTYPE( void uart_write, (tty_t *tp)			);
FORWARD _PROTOTYPE( void uart_echo, (tty_t *tp, int c)			);
FORWARD _PROTOTYPE( void uart_read, (tty_t *tp)				);
FORWARD _PROTOTYPE( int uart_hw_int, (irq_hook_t *hook)			);

PUBLIC void putk(c)
int c;
{
/* The kernel's printf. */
  if (c == 0) return;
  if (c == '\n') UART_DATA = '\r';
  UART_DATA = c;
}

PRIVATE void out_char(c)
int c;
{
  if (c == '\n') UART_DATA = '\r';
  UART_DATA = c & 0xff;
}

PRIVATE void uart_write(tp)
register struct tty *tp;
{
/* Copy the user's bytes out, then reply to the writer. */
  int count, i;
  char buf[64];
  phys_bytes user_phys;

  if ((count = tp->tty_outleft) == 0 || tp->tty_inhibited) return;
  do {
	if (count > sizeof(buf)) count = sizeof(buf);
	user_phys = proc_vir2phys(proc_addr(tp->tty_outproc), tp->tty_out_vir);
	phys_copy(user_phys, vir2phys(buf), (phys_bytes) count);
	tp->tty_out_vir += count;
	tp->tty_outcum += count;
	tp->tty_outleft -= count;
	for (i = 0; i < count; i++) out_char(buf[i]);
  } while ((count = tp->tty_outleft) != 0 && !tp->tty_inhibited);

  if (tp->tty_outleft == 0) {
	tty_reply(tp->tty_outrepcode, tp->tty_outcaller, tp->tty_outproc,
							tp->tty_outcum);
	tp->tty_outcum = 0;
  }
}

PRIVATE void uart_echo(tp, c)
tty_t *tp;
int c;
{
  out_char(c);
}

PRIVATE void uart_read(tp)
tty_t *tp;
{
/* Process the characters in the buffer of the interrupt handler. */
  char buf[1];
  int ch;

  tp = &tty_table[0];
  while (icount > 0) {
	ch = *itail++;
	if (itail == ibuf + IN_BYTES) itail = ibuf;
	lock();
	icount--;
	unlock();
	if (ch == 0x7f) ch = '\b';	/* the usual key for erase */
	buf[0] = ch;
	(void) in_process(tp, buf, 1);
  }
}

PRIVATE int uart_hw_int(hook)
irq_hook_t *hook;
{
/* A byte has arrived: queue what there is and wake up the tty task. */
  while ((UART_STATUS & 2) != 0) {
	if (icount < IN_BYTES) {
		*ihead++ = UART_DATA;
		if (ihead == ibuf + IN_BYTES) ihead = ibuf;
		icount++;
	} else {
		(void) UART_DATA;
	}
  }
  tty_table[0].tty_events = 1;
  force_timeout();
  return(1);
}

PUBLIC void scr_init(tp)
tty_t *tp;
{
/* The terminal is the UART. */
  static irq_hook_t uart_hook;

  if (tp != &tty_table[0]) return;
  tp->tty_devwrite = uart_write;
  tp->tty_echo = uart_echo;
  tp->tty_devread = uart_read;
  tp->tty_winsize.ws_row = 24;
  tp->tty_winsize.ws_col = 80;
  put_irq_handler(&uart_hook, UART_IRQ, uart_hw_int);
  enable_irq(&uart_hook);
}

PUBLIC void kb_init(tp)
tty_t *tp;
{
}

PUBLIC void cons_stop()
{
}

PUBLIC void toggle_scroll()
{
}

PUBLIC void select_console(cons_line)
int cons_line;
{
}

PUBLIC int con_loadfont(user_phys)
phys_bytes user_phys;
{
  return(ENOTTY);
}

PUBLIC int kbd_loadmap(user_phys)
phys_bytes user_phys;
{
  return(ENOTTY);
}
