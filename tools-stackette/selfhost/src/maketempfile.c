#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include "system.h"
#include "astring.h"

char* sys_maketempfile(const char* prefix, const char* suffix)
{
	static int count;
	const char* tempdir = sys_gettmpdir();

	for (;;)
	{
		char* filename = aprintf("%s/ack.%s-%x%x%s%s",
			tempdir, prefix, (unsigned)getpid(), ++count,
			((*suffix == 0) || (*suffix == '.')) ? "" : ".",
			suffix);
		int fd = open(filename, O_CREAT|O_EXCL|O_RDWR, 0600);
		if (fd != -1)
		{
			close(fd);
			return filename;
		}
		free(filename);
	}
}
