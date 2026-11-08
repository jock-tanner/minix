/* hw23: the Stackette kernel in C, the first step of the Minix port: the
 * system context is a loop around reti, the processes are C programs loaded
 * from the table in the flash, the system calls are traps.
 *
 * Expect ABCDEFGHTZ, then silence.
 */

typedef unsigned u16;

#define CTX(i)		(*(volatile u16 *) (0xf020 + 2 * (i)))
#define CTXID		(*(volatile u16 *) 0xf02e)
#define UART		(*(volatile u16 *) 0xf000)
#define SMMU(sp, pg)	(*(volatile u16 *) (0xf100 + 2 * ((sp) * 16 + (pg))))
#define UMMU(sp, pg)	(*(volatile u16 *) (0xf180 + 2 * ((sp) * 16 + (pg))))

#define CODE		0
#define RSTACK		1
#define PSTACK		2
#define DATA		3
#define FLASH		0x2000
#define NOMEM		0x4000

#define WIN_SRC		11
#define WIN_DST		12
#define WIN_USER	13
#define WIN(w)		((u16 *) ((w) << 12))

#define R_IP		0
#define R_RSP		1
#define R_PFP		2
#define R_MBP		3
#define R_MIDX		4
#define R_SW		5
#define R_PSP		6

#define NPROC		4
#define RUNNING		0
#define DEAD		1

#define ID_TRAP		2
#define ID_FAULT	3

struct proc {
  u16 reg[7];
  u16 state;
  u16 tpage, ntpages;		/* the flash pages of the text */
  u16 dpage, ndpages;		/* the RAM pages of the data segment */
  u16 rpage;			/* the RAM page of the return stack */
};

extern u16 table_page;
void ctxrun(void);

struct proc ptab[NPROC];
int nproc;
int cur;
u16 next_page = 0x10;		/* the next free RAM page */

u16 *window(w, page)
int w;
u16 page;
{
  SMMU(DATA, w) = page;
  return WIN(w);
}

void putc_(c)
int c;
{
  UART = c;
}

/* the word at the user's virtual address `a` in the data space of p */
u16 uread(p, a)
struct proc *p;
u16 a;
{
  u16 *w = window(WIN_USER, p->dpage + (a >> 12));

  return w[(a & 0xfff) >> 1];
}

void uwrite(p, a, v)
struct proc *p;
u16 a, v;
{
  u16 *w = window(WIN_USER, p->dpage + (a >> 12));

  w[(a & 0xfff) >> 1] = v;
}

/* the result of a system call: pushed on the parameter stack of the process */
void upush(p, v)
struct proc *p;
u16 v;
{
  p->reg[R_PSP] -= 2;
  uwrite(p, p->reg[R_PSP], v);
}

void savectx(p)
struct proc *p;
{
  int i;

  for (i = 0; i < 7; i++) p->reg[i] = CTX(i);
}

void loadctx(p)
struct proc *p;
{
  int i, sp;

  for (i = 0; i < 7; i++) CTX(i) = p->reg[i];
  for (sp = 0; sp < 4; sp++)
	for (i = 0; i < 16; i++) UMMU(sp, i) = NOMEM;
  for (i = 0; i < p->ntpages; i++) UMMU(CODE, i) = p->tpage + FLASH + i;
  for (i = 0; i < p->ndpages; i++) {
	UMMU(DATA, i) = p->dpage + i;
	UMMU(PSTACK, i) = p->dpage + i;
  }
  UMMU(RSTACK, 15) = p->rpage;
}

/* the next process that can run, after the current one; -1 if none */
int sched()
{
  int i, k;

  for (i = 1; i <= nproc; i++) {
	k = (cur + i) % nproc;
	if (ptab[k].state == RUNNING) return k;
  }
  return -1;
}

void load_proc(n)
int n;
{
  struct proc *p = &ptab[n];
  u16 *e, *s, *d;
  u16 tpage, ntp, dpage, dlen, total, k, i, left, top, psp;

  e = window(WIN_SRC, FLASH + table_page);
  e += 2 + 8 * n;
  p->tpage = e[0];
  p->ntpages = e[1];
  dpage = e[2];
  dlen = e[3];
  total = e[5];
  p->ndpages = total >> 12;
  p->dpage = next_page;
  next_page += p->ndpages;
  p->rpage = next_page++;

  /* the data segment: cleared, then the image of the rom and the data */
  for (k = 0; k < p->ndpages; k++) {
	d = window(WIN_DST, p->dpage + k);
	for (i = 0; i < 2048; i++) d[i] = 0;
  }
  left = dlen;
  for (k = 0; left > 0; k++) {
	s = window(WIN_SRC, FLASH + dpage + k);
	d = window(WIN_DST, p->dpage + k);
	for (i = 0; i < 2048 && left > 0; i++) {
		d[i] = s[i];
		left -= 2;
		if (left > dlen) left = 0;
	}
  }

  /* argc = 1, argv = {"p", 0}, envp = {0} at the top of the data segment */
  top = total - 2;
  uwrite(p, top, 'p');
  psp = top - 8;
  uwrite(p, psp, 1);
  uwrite(p, psp + 2, top);
  p->reg[R_IP] = 0;
  p->reg[R_RSP] = 0;
  p->reg[R_PFP] = psp;
  p->reg[R_MBP] = 0;
  p->reg[R_MIDX] = 0;
  p->reg[R_SW] = 0;
  p->reg[R_PSP] = psp;
  p->state = RUNNING;
}

void trap(p, n)
struct proc *p;
int n;
{
  switch (n) {
  case 8:			/* putc(c) */
	putc_(uread(p, p->reg[R_PFP]));
	upush(p, 0);
	break;
  case 9:			/* exit() */
	p->state = DEAD;
	break;
  case 10:			/* yield() */
	upush(p, 0);
	break;
  default:
	putc_('?');
	for (;;) ;
  }
}

void kmain()
{
  struct proc *p;
  u16 id;
  int i, next;

  putc_('A');
  for (nproc = 0; nproc < 2; nproc++) load_proc(nproc);
  cur = 0;
  loadctx(&ptab[0]);
  for (;;) {
	p = &ptab[cur];
	ctxrun();
	id = CTXID;
	savectx(p);
	switch (id >> 6) {
	case ID_TRAP:
		trap(p, id & 63);
		break;
	case ID_FAULT:
		if ((id & 63) == 1) {	/* a request was refused */
			putc_('T');
			p->state = DEAD;
		} else {
			putc_('?');
			for (;;) ;
		}
		break;
	default:
		putc_('!');
		for (;;) ;
	}
	next = sched();
	if (next < 0) {
		putc_('Z');
		for (;;) ;
	}
	if (next != cur || p->state == DEAD) {
		cur = next;
		loadctx(&ptab[cur]);
	}
  }
}
