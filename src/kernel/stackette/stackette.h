/* The hardware of the Stackette as the kernel sees it (doc/kernel.md). */

#define PERIPH(a)	(*(volatile unsigned *) (a))
#define CTX(i)		PERIPH(0xf020 + 2 * (i))	/* the context device */
#define CTXID		PERIPH(0xf02e)
#define INTC_RAW	PERIPH(0xf008)
#define UART_DATA	PERIPH(0xf000)
#define UART_STATUS	PERIPH(0xf002)
#define SMMU(sp, pg)	PERIPH(0xf100 + 2 * ((sp) * 16 + (pg)))
#define UMMU(sp, pg)	PERIPH(0xf180 + 2 * ((sp) * 16 + (pg)))

/* The address spaces. */
#define SP_CODE		0
#define SP_RSTACK	1
#define SP_PSTACK	2
#define SP_DATA		3

/* The physical pages: the RAM is below 0x2000, the flash from 0x2000. */
#define PAGE_SHIFT	12
#define PAGE_SIZE	4096
#define RAM_PAGES	0x2000
#define FLASH_PAGE	0x2000
#define NOMEM_PAGE	0x4000	/* any access is refused */
#define PERIPH_PAGE	0x8000	/* the peripherals */

/* The overlays (doc: the code space has OVL_ROOT_PAGES pages of code that
 * is always there, and a window of OVL_WIN_PAGES pages above it, where the
 * code of one overlay at a time is mapped. A text segment of more than 16
 * pages has the root first, then the overlays one after the other.
 */
#define OVL_ROOT_PAGES	12
#define OVL_WIN_PAGES	4

/* The windows of the system context (data space pages 11, 12 and 13). */
#define WIN_SRC		11
#define WIN_DST		12
#define WIN_USER	13
#define WINDOW(w)	((unsigned *) ((unsigned) (w) << PAGE_SHIFT))
#define BWINDOW(w)	((unsigned char *) ((unsigned) (w) << PAGE_SHIFT))

/* The kernel's memory. */
#define KDATA_PAGES	11	/* pages 0 to 10 of the data space */
#define RSTACK_POOL	0x10	/* the return stacks of the process table slots */
#define FIRST_USER_PAGE	0x40	/* the memory that MM manages starts here */

/* The registers of a context, the order of struct stackframe_s. */
#define R_IP		0
#define R_RSP		1
#define R_PFP		2
#define R_MBP		3
#define R_MIDX		4
#define R_SW		5
#define R_PSP		6
#define NR_CTX_REGS	7

/* The reasons the system context is entered: CTXID >> 6. */
#define ID_IRQ		1
#define ID_TRAP		2
#define ID_FAULT	3

/* Traps. 1, 2, 3 are the SEND, RECEIVE and BOTH of Minix. */
#define TRAP_KCALL	4	/* a task asks the system context for a service */
#define TRAP_OVLCALL	5	/* a call to a function in another overlay */
#define TRAP_OVLRET	6	/* the return from it */
#define TRAP_SETJMP	32
#define TRAP_LONGJMP	33

/* The services of TRAP_KCALL: kcall(code, &args). */
#define KC_PHYS_COPY	1
#define KC_REBOOT	2
#define KC_CAUSE_SIG	3
#define KC_UWRITE	4
#define KC_RPUSH	5

struct kc_w {
  int proc_nr;
  unsigned addr, value;
  int result;
};

struct kc_sig {
  int proc_nr, sig_nr;
};

struct kc_copy {
  phys_bytes src, dst, n;
};

_PROTOTYPE( int kcall, (int code, void *args)				);
_PROTOTYPE( void wcopy, (unsigned *src, unsigned *dst, unsigned n)	);
_PROTOTYPE( void ctxrun, (void)						);
_PROTOTYPE( void phys_copy_sys, (phys_bytes src, phys_bytes dst,
							phys_bytes n)	);
_PROTOTYPE( unsigned uread, (struct proc *rp, vir_bytes addr)		);
_PROTOTYPE( unsigned rread, (struct proc *rp, unsigned rsp)		);

/* Physical addresses are longs; shifting a long is a loop, so the page and the
 * offset are taken from the words.
 */
union physw {
  phys_bytes l;
  unsigned w[2];		/* low word first */
};

_PROTOTYPE( unsigned phys_page, (phys_bytes a)				);
_PROTOTYPE( phys_bytes phys_make, (unsigned page, unsigned offset)	);
