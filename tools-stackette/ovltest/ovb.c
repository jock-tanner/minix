#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>

extern int c(int x);
extern int even(int n);

int b(int x)
{
	return c(x) + 100;
}

int odd(int n)
{
	if (n == 0) return 0;
	return even(n - 1);
}

int forker(void)
{
	int st;
	pid_t p = fork();

	if (p == 0) {
		/* the child returns through the overlay stack too */
		_exit(even(4) + 41);
	}
	waitpid(p, &st, 0);
	return (st >> 8) & 0xff;
}
