/* The C side of setjmp and longjmp on the Stackette. The kernel does the
 * jump itself (setjmp.s); here are the signal mask, which is saved in the
 * buffer before the trap, and the calls of longjmp.
 */
#include <sys/types.h>
#include <signal.h>
#include <stdlib.h>
#include <setjmp.h>
#include <sys/sigcontext.h>

int _sigprocmask(int how, const sigset_t *set, sigset_t *oset);
int _kjmp(void *buf, int val);

void __setjmpsave(env, save)
jmp_buf env;
int save;
{
  if (save) {
	env[0].__flags = SC_SIGCONTEXT;
	_sigprocmask(SIG_SETMASK, (sigset_t *) NULL, (sigset_t *) &env[0].__mask);
  } else {
	env[0].__flags = 0;
  }
}

void longjmp(env, val)
jmp_buf env;
int val;
{
  if (env[0].__flags & SC_SIGCONTEXT)
	_sigprocmask(SIG_SETMASK, (sigset_t *) &env[0].__mask, (sigset_t *) NULL);
  _kjmp((void *) &env[0].__pc, val);
  abort();			/* the kernel refused: the frame is gone */
}

void _longjmp(env, val)
jmp_buf env;
int val;
{
  _kjmp((void *) &env[0].__pc, val);
  abort();
}
