/* Miscellaneous system calls.				Author: Kees J. Bot
 *								31 Mar 2000
 *
 * The entry points into this file are:
 *   do_reboot: kill all processes, then reboot system
 *   do_svrctl: memory manager control
 */

#include "mm.h"
#include <minix/callnr.h>
#include <signal.h>
#include <sys/svrctl.h>
#include "mproc.h"
#include "param.h"

/*=====================================================================*
 *			    do_reboot				       *
 *=====================================================================*/
PUBLIC int do_reboot()
{
  register struct mproc *rmp = mp;
  char monitor_code[32*sizeof(char *)];

  if (rmp->mp_effuid != SUPER_USER) return(EPERM);

  switch (reboot_flag) {
  case RBT_HALT:
  case RBT_REBOOT:
  case RBT_PANIC:
  case RBT_RESET:
	break;
  case RBT_MONITOR:
	if (reboot_size >= sizeof(monitor_code)) return(EINVAL);
	if (sys_copy(who, D, (phys_bytes) reboot_code,
		MM_PROC_NR, D, (phys_bytes) monitor_code,
		(phys_bytes) (reboot_size+1)) != OK) return(EFAULT);
	if (monitor_code[reboot_size] != 0) return(EINVAL);
	break;
  default:
	return(EINVAL);
  }

  /* Kill all processes except init. */
  check_sig(-1, SIGKILL);

  tell_fs(REBOOT,0,0,0);		/* tell FS to prepare for shutdown */

  sys_abort(reboot_flag, MM_PROC_NR, monitor_code, reboot_size);
  /* NOTREACHED */
}

/*=====================================================================*
 *			    do_svrctl				       *
 *=====================================================================*/
PUBLIC int do_svrctl()
{
  int req;
  vir_bytes ptr;

  req = svrctl_req;
  ptr = (vir_bytes) svrctl_argp;

  /* Is the request for the kernel? */
  if (((req >> 8) & 0xFF) == 'S') {
	return(sys_sysctl(who, req, mp->mp_effuid == SUPER_USER, ptr));
  }

  switch(req) {
  case MMSIGNON: {
	/* A user process becomes a task.  Simulate an exit by
	 * releasing a waiting parent and disinheriting children.
	 */
	struct mproc *rmp;
	pid_t pidarg;

	if (mp->mp_effuid != SUPER_USER) return(EPERM);

	rmp = &mproc[mp->mp_parent];
	tell_fs(EXIT, who, 0, 0);

	pidarg = rmp->mp_wpid;
	if ((rmp->mp_flags & WAITING) && (pidarg == -1
		|| pidarg == mp->mp_pid || -pidarg == mp->mp_procgrp))
	{
		/* Wake up the parent. */
		rmp->reply_res2 = 0;
		setreply(mp->mp_parent, mp->mp_pid);
		rmp->mp_flags &= ~WAITING;
	}

	/* Disinherit children. */
	for (rmp = &mproc[0]; rmp < &mproc[NR_PROCS]; rmp++) {
		if (rmp->mp_flags & IN_USE && rmp->mp_parent == who) {
			rmp->mp_parent = INIT_PROC_NR;
		}
	}

	/* Become like MM and FS. */
	mp->mp_pid = mp->mp_procgrp = 0;
	mp->mp_parent = 0;
	return(OK); }

#if ENABLE_SWAP
  case MMSWAPON: {
	struct mmswapon swapon;

	if (mp->mp_effuid != SUPER_USER) return(EPERM);

	if (sys_copy(who, D, (phys_bytes) ptr,
		MM_PROC_NR, D, (phys_bytes) &swapon,
		(phys_bytes) sizeof(swapon)) != OK) return(EFAULT);

	return(swap_on(swapon.file, swapon.offset, swapon.size)); }

  case MMSWAPOFF: {
	if (mp->mp_effuid != SUPER_USER) return(EPERM);

	return(swap_off()); }
#endif /* SWAP */

  default:
	return(EINVAL);
  }
}

#if (CHIP == STACKETTE)
/*=====================================================================*
 *			    do_pool, free_pool				       *
 *=====================================================================*/
PUBLIC int do_pool()
{
/* pool(pages, window, wn): the process has a pool of `pages` pages of memory
 * outside of its 64K, and a window of wn pages at the page aligned address
 * `window`, in its data space, to look at them (kernel, TRAP_WINMAP). A
 * second call gives it a new pool: the old contents that fit are kept. The
 * contents of a pool are not set to zero.
 */
  unsigned pages = (unsigned) pool_pages, wn = (unsigned) pool_wn;
  vir_bytes win = (vir_bytes) pool_win;
  phys_clicks new_base = 0, old_base = mp->mp_pool;
  unsigned old_len = mp->mp_plen, keep;
  int r;

  if (pages > MAX_POOL || wn > 8 || (pages != 0 && wn == 0)) return(EINVAL);
  if (pages == 0) wn = 0;
  if (wn != 0 && ((win & (CLICK_SIZE - 1)) != 0 ||
	(win >> CLICK_SHIFT) + wn > mp->mp_seg[D].mem_len)) return(EINVAL);
  if (pages != 0) {
	if ((new_base = alloc_mem((phys_clicks) pages)) == NO_MEM) return(ENOMEM);
	keep = old_len < pages ? old_len : pages;
	if (keep != 0) {
		r = sys_copy(ABS, 0, (phys_bytes) old_base << CLICK_SHIFT, ABS, 0,
			(phys_bytes) new_base << CLICK_SHIFT,
			(phys_bytes) keep << CLICK_SHIFT);
		if (r < 0) panic("do_pool can't copy", r);
	}
  }
  if (old_len != 0) free_mem(old_base, (phys_clicks) old_len);
  mp->mp_pool = new_base;
  mp->mp_plen = pages;
  mp->mp_wbase = win >> CLICK_SHIFT;
  mp->mp_wn = wn;
  sys_winset(who, new_base, pages, win >> CLICK_SHIFT, wn, 0);
  return(OK);
}

PUBLIC void free_pool(rmp)
struct mproc *rmp;
{
/* The process is gone or has a new image: the pool is released. */
  if (rmp->mp_plen != 0) free_mem(rmp->mp_pool, (phys_clicks) rmp->mp_plen);
  rmp->mp_plen = 0;
  rmp->mp_wn = 0;
}
#endif
