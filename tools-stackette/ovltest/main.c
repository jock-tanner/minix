#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

extern int a(int x);
extern int b(int x);
extern int even(int n);
extern int forker(void);

int c(int x)
{
	return x + 1000;
}

int main(argc, argv)
int argc;
char **argv;
{
	int (*fp)(int) = b;
	int s;

	printf("a=%d\n", a(1));		/* 1 -> a (+10) -> b (+100) -> c (+1000): 1111 */
	printf("fp=%d\n", fp(5));	/* 5 -> b -> c: 1105 */
	printf("even=%d %d\n", even(6), even(7));	/* 1 0 */
	s = forker();
	printf("fork=%d\n", s);	/* 42 */
	return 0;
}
