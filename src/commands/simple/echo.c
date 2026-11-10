/* echo - print the arguments */

#include <sys/types.h>
#include <string.h>
#include <unistd.h>

int main(argc, argv)
int argc;
char **argv;
{
  int i, nl = 1;

  if (argc > 1 && strcmp(argv[1], "-n") == 0) {
	nl = 0;
	argc--;
	argv++;
  }
  for (i = 1; i < argc; i++) {
	if (i > 1) write(1, " ", 1);
	write(1, argv[i], strlen(argv[i]));
  }
  if (nl) write(1, "\n", 1);
  return(0);
}
