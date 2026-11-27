#include <stdlib.h>
#include <string.h>
#include "system.h"

#define ESCAPECHAR '\\'

static int needs_escaping(char c)
{
	switch (c)
	{
		case ' ':
		case '"':
		case '\'':
		case '\\':
		case '(':
		case ')':
			return 1;
		default:
			return 0;
	}
}

static char* append_escaped(char* buffer, const char* word)
{
	for (;;)
	{
		char c = *word++;
		if (!c)
			break;
		if (needs_escaping(c))
			*buffer++ = ESCAPECHAR;
		*buffer++ = c;
	}
	return buffer;
}

int sys_system(const char* prog, const char* const* arglist)
{
	const char* const* arg;
	int len;
	char* cmdline;
	char* p;
	int status;

	len = strlen(prog) * 2 + 4;
	for (arg = arglist + 1; *arg; arg++)
		len += strlen(*arg) * 2 + 1;
	cmdline = malloc(len + 1);
	p = append_escaped(cmdline, prog);
	for (arg = arglist + 1; *arg; arg++)
	{
		*p++ = ' ';
		p = append_escaped(p, *arg);
	}
	*p = 0;
	status = system(cmdline);
	free(cmdline);
	return status;
}
