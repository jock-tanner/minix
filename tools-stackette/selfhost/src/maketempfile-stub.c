#include <string.h>
char *strdup(const char *s);

char *sys_maketempfile(const char *prefix, const char *suffix)
{
	return strdup("/tmp/ack.opt.dat");
}
