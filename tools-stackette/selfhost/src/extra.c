#include <stdlib.h>
#include <string.h>
char *strdup(const char *s)
{
	char *p = malloc(strlen(s) + 1);
	if (p != NULL)
		strcpy(p, s);
	return p;
}
int sys_close(void *f) { return 0; }
