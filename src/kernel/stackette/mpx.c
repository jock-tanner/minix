/* The system context of the Stackette: the loop that Minix's mpx.s is. The
 * kernel proper (proc.c, system.c, the interrupt handlers) runs here, in a
 * coroutine around `reti`: ctxrun() resumes the user context, which is a task
 * or a process, and returns when that traps, faults or is interrupted. The
 * system context is never interrupted, so none of the kernel's critical
 * regions needs a lock against it.
 *
 * Trap 1, 2, 3: send, receive, sendrec (the SEND, RECEIVE, BOTH of Minix);
 * 4: a service of the system context for a task; 32, 33: setjmp, longjmp.
 * The result of a trap is pushed on the parameter stack of the caller.
 */

#include "../kernel.h"
#include <signal.h>
#include <unistd.h>
#include <minix/callnr.h>
#include <minix/com.h>
#include "../proc.h"
#include "stackette.h"

PRIVATE struct proc *loaded;	/* the process whose map is in the user MMU */

FORWARD _PROTOTYPE( void loadmap, (struct proc *rp)			);
FORWARD _PROTOTYPE( void interrupt_entry, (int irq)			);
FORWARD _PROTOTYPE( void fault_entry, (struct proc *rp, int f)		);
FORWARD _PROTOTYPE( void trap_entry, (struct proc *rp, int n)		);
FORWARD _PROTOTYPE( void do_kcall, (struct proc *rp)			);
FORWARD _PROTOTYPE( void do_setjmp, (struct proc *rp)			);
FORWARD _PROTOTYPE( void do_longjmp, (struct proc *rp)			);
FORWARD _PROTOTYPE( void upush, (struct proc *rp, unsigned v)		);

/*===========================================================================*
 *				window					     *
 *===========================================================================*/
PRIVATE unsigned *window(w, page)
int w;
unsigned page;
{
/* Make the window w a view of the physical page. */
  SMMU(SP_DATA, w) = page;
  return(WINDOW(w));
}

/*===========================================================================*
 *				uread, uwrite				     *
 *===========================================================================*/
PUBLIC unsigned uread(rp, addr)
struct proc *rp;
vir_bytes addr;
{
/* The word at the virtual address (in the data space) of a process; 0xffff
 * if it is not mapped.
 */
  phys_bytes pa;
  unsigned *w;

  if ((addr & 1) != 0 || (pa = umap(rp, D, addr, 2)) == 0) return(0xffff);
  w = window(WIN_USER, phys_page(pa));
  return(w[((unsigned) pa & (PAGE_SIZE - 1)) >> 1]);
}

PUBLIC int uwrite(rp, addr, value)
struct proc *rp;
vir_bytes addr;
reg_t value;
{
  phys_bytes pa;
  unsigned *w;

  if ((addr & 1) != 0 || (pa = umap(rp, D, addr, 2)) == 0) return(EFAULT);
  w = window(WIN_USER, phys_page(pa));
  w[((unsigned) pa & (PAGE_SIZE - 1)) >> 1] = value;
  return(OK);
}

/*===========================================================================*
 *				rread, rpush				     *
 *===========================================================================*/
PUBLIC unsigned rread(rp, rsp)
struct proc *rp;
unsigned rsp;
{
/* The word at rsp of the return stack of a process. */
  unsigned *w = window(WIN_USER, rp->p_rpage);

  return(w[(rsp & (PAGE_SIZE - 1)) >> 1]);
}

PUBLIC int rpush(rp, value)
struct proc *rp;
reg_t value;
{
/* Push on the return stack of a process (not running). */
  unsigned *w = window(WIN_USER, rp->p_rpage);

  rp->p_reg.rsp -= 2;
  w[(rp->p_reg.rsp & (PAGE_SIZE - 1)) >> 1] = value;
  return(OK);
}

PRIVATE void rwrite(rp, rsp, value)
struct proc *rp;
unsigned rsp, value;
{
  unsigned *w = window(WIN_USER, rp->p_rpage);

  w[(rsp & (PAGE_SIZE - 1)) >> 1] = value;
}

/*===========================================================================*
 *				upush					     *
 *===========================================================================*/
PRIVATE void upush(rp, v)
struct proc *rp;
unsigned v;
{
/* The result of a system call goes on the parameter stack of the caller. */
  rp->p_reg.sp -= 2;
  (void) uwrite(rp, rp->p_reg.sp, v);
}

/*===========================================================================*
 *				alloc_segments				     *
 *===========================================================================*/
PUBLIC void alloc_segments(rp)
register struct proc *rp;
{
/* Called by main() and do_newmap(). The map goes to the user MMU when the
 * process runs; what it needs besides is the page of its return stack.
 */
  rp->p_rpage = RSTACK_POOL + NR_TASKS + proc_number(rp);
  if (rp == loaded) loaded = NIL_PROC;
}

/*===========================================================================*
 *				loadmap					     *
 *===========================================================================*/
PRIVATE void loadmap(rp)
struct proc *rp;
{
/* Fill the user MMU from the memory map of the process: the text at the
 * virtual address 0 of the code space, the data and the stack at 0 of the data
 * space and the parameter stack space, the return stack page; what is not
 * mapped refuses every access. A task also sees the peripherals.
 */
  register volatile unsigned *code = &UMMU(SP_CODE, 0);
  register volatile unsigned *rstack = &UMMU(SP_RSTACK, 0);
  register volatile unsigned *pstack = &UMMU(SP_PSTACK, 0);
  register volatile unsigned *data = &UMMU(SP_DATA, 0);
  register unsigned i, n, phys;

  for (i = 0; i < 16; i++) {
	code[i] = NOMEM_PAGE;
	rstack[i] = NOMEM_PAGE;
	pstack[i] = NOMEM_PAGE;
	data[i] = NOMEM_PAGE;
  }
  n = rp->p_map[T].mem_len;
  phys = rp->p_map[T].mem_phys;
  for (i = 0; i < n; i++) {
	*code++ = phys;
	phys++;
  }
  n = rp->p_map[D].mem_len;
  phys = rp->p_map[D].mem_phys;
  for (i = 0; i < n; i++) {
	*data++ = phys;
	*pstack++ = phys;
	phys++;
  }
  rstack[15] = rp->p_rpage;
  if (istaskp(rp)) UMMU(SP_DATA, 15) = PERIPH_PAGE;
  loaded = rp;
}

/*===========================================================================*
 *				restart					     *
 *===========================================================================*/
PUBLIC void restart()
{
/* The loop of the system context: run the process that proc.c has chosen until
 * it traps, faults or is interrupted, and deal with that. Never returns.
 */
  struct proc *rp;
  unsigned *regs;
  int i, id;

  for (;;) {
	if (held_head != NIL_PROC) unhold();	/* flush the held-up interrupts */
	rp = proc_ptr;
	regs = (unsigned *) &rp->p_reg;
	for (i = 0; i < NR_CTX_REGS; i++) CTX(i) = regs[i];
	if (loaded != rp) loadmap(rp);
	in_kernel = FALSE;
	PERIPH(0xf00a) = kernel_mask;	/* the lines that are open */
	ctxrun();
	in_kernel = TRUE;
	id = CTXID;
	regs = (unsigned *) &rp->p_reg;
	for (i = 0; i < NR_CTX_REGS; i++) regs[i] = CTX(i);

	switch (id >> 6) {
	case ID_IRQ:	interrupt_entry(id & 63);	break;
	case ID_TRAP:	trap_entry(rp, id & 63);	break;
	case ID_FAULT:	fault_entry(rp, id & 63);	break;
	default:	panic("bad context id", id);
	}
  }
}

/*===========================================================================*
 *				interrupt_entry				     *
 *===========================================================================*/
PRIVATE void interrupt_entry(irq)
int irq;
{
/* A hardware interrupt. The line is closed while its handlers run, and opens
 * again when every handler has finished with it (the others later, through
 * enable_irq()).
 */
  kernel_mask &= ~(1 << irq);
  if ((unsigned) irq >= NR_IRQ_VECTORS || irq_hooks[irq] == NULL) return;
  intr_handle(irq_hooks[irq]);
  if (irq_actids[irq] == 0) kernel_mask |= 1 << irq;
}

/*===========================================================================*
 *				fault_entry				     *
 *===========================================================================*/
PRIVATE void fault_entry(rp, f)
struct proc *rp;
int f;
{
/* An exception of the process: a signal to it, unless it is a task. */
  int sig;

  switch (f) {
  case 0:	sig = SIGILL;	break;
  case 4:	sig = SIGFPE;	break;
  default:	sig = SIGSEGV;	break;
  }
  if (!isuserp(rp)) {
	printf("\nfault %d of %s at %x, address %x\n", f, rp->p_name,
						rp->p_reg.pc, PERIPH(0xf032));
	panic("exception in a system process", NO_NUM);
  }
  cause_sig(proc_number(rp), sig);
}

/*===========================================================================*
 *				trap_entry				     *
 *===========================================================================*/
PRIVATE void trap_entry(rp, n)
struct proc *rp;
int n;
{
  unsigned pfp, a, b;
  int r;

  switch (n) {
  case SEND:
  case RECEIVE:
  case BOTH:
	pfp = rp->p_reg.fp;
	a = uread(rp, pfp);
	b = uread(rp, pfp + 2);
	if (a == 0xffff && b == 0xffff) {
		r = EFAULT;
	} else {
		r = sys_call(n, (int) a, (message *) b);
	}
	upush(rp, r);
	break;
  case TRAP_KCALL:	do_kcall(rp);		break;
  case TRAP_SETJMP:	do_setjmp(rp);		break;
  case TRAP_LONGJMP:	do_longjmp(rp);		break;
  default:
	upush(rp, E_BAD_FCN);
  }
}

/*===========================================================================*
 *				do_kcall				     *
 *===========================================================================*/
PRIVATE void do_kcall(rp)
struct proc *rp;
{
/* kcall(code, args): a task asks for what only the system context can do.
 * The arguments are in the task's stack, which is kernel data (identity mapped).
 */
  unsigned pfp, code, args;
  struct kc_copy *kc;

  if (!istaskp(rp)) {
	upush(rp, EPERM);
	return;
  }
  pfp = rp->p_reg.fp;
  code = uread(rp, pfp);
  args = uread(rp, pfp + 2);
  switch (code) {
  case KC_PHYS_COPY:
	kc = (struct kc_copy *) args;
	phys_copy_sys(kc->src, kc->dst, kc->n);
	break;
  case KC_REBOOT:
	wreboot(RBT_HALT);
	break;
  }
  upush(rp, OK);
}

/*===========================================================================*
 *				do_setjmp, do_longjmp			     *
 *===========================================================================*/
/* The jump buffer, in the data space of the caller (kernel.md section 7):
 * 0 the return address of the setjmp call, 1 the PFP of its caller, 2 the RSP
 * after setjmp returns, 3 the PSP after it.
 */
PRIVATE void do_setjmp(rp)
struct proc *rp;
{
  unsigned buf = uread(rp, rp->p_reg.fp);
  int ok;

  ok = uwrite(rp, buf, rread(rp, rp->p_reg.rsp)) == OK;
  ok = ok && uwrite(rp, buf + 2, rread(rp, rp->p_reg.rsp + 2)) == OK;
  ok = ok && uwrite(rp, buf + 4, rp->p_reg.rsp + 4) == OK;
  ok = ok && uwrite(rp, buf + 6, rp->p_reg.fp) == OK;
  upush(rp, ok ? 0 : 0xffff);
}

PRIVATE void do_longjmp(rp)
struct proc *rp;
{
  unsigned buf = uread(rp, rp->p_reg.fp);
  unsigned val = uread(rp, rp->p_reg.fp + 2);
  unsigned ip, pfp, rsp, psp;

  ip = uread(rp, buf);
  pfp = uread(rp, buf + 2);
  rsp = uread(rp, buf + 4);
  psp = uread(rp, buf + 6);
  /* the frame of setjmp must be alive: not below the caller of longjmp */
  if (buf == 0xffff || rsp < rp->p_reg.rsp + 4 || psp == 0xffff) {
	upush(rp, 0xffff);
	return;
  }
  if (val == 0) val = 1;
  rp->p_reg.pc = ip;
  rp->p_reg.fp = pfp;
  rp->p_reg.rsp = rsp;
  rp->p_reg.sp = psp;
  (void) uwrite(rp, psp, val);
}

/*===========================================================================*
 *				idle_task				     *
 *===========================================================================*/
PUBLIC void idle_task()
{
/* The kernel runs only when something interrupts the user context, so the
 * idle process just waits.
 */
  while (TRUE) {}
}
