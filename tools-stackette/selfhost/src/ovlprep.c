/*
 * ovlprep: prepares the assembly files of a program with overlays; this is
 * tools-stackette/ovlprep.py for the target (read that for what it does):
 *
 *	ovlprep -o outdir [-t thunks.s] group0 file.s ... + group1 file.s ... [+ ...]
 *
 * It writes outdir/<file>.s, the changed copy of every file, and thunks.s, the
 * stubs for the functions of all overlays. A file is read three times (the
 * functions, the static functions whose address is taken, the copy) so that
 * it does not have to be held in memory; the output is the same as the Python
 * program's.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LINEMAX 1024
#define RENMAX (LINEMAX + 1024)
#define POOLSIZE 18000
#define MAXSYM 1200
#define HASHSIZE 1024
#define MAXPATH 256

#define F_EXT 1
#define F_FUNC 2
#define F_ESC 4
#define F_MINE 8

static const char header[] = ".sect .text; .sect .rom; .sect .data; .sect .bss";

struct sym {
	char* name;
	int flags;
	int next;
};

static char pool[POOLSIZE];
static int poolpos;
static struct sym syms[MAXSYM];
static int nsym;
static int table[HASHSIZE];
static int funcorder[MAXSYM];
static int nfunc;
static int escorder[MAXSYM];
static int nesc;
static int newname[MAXSYM];
static char line[LINEMAX];
static char ren[RENMAX];
static char tmp[MAXPATH + 64];
static char base[MAXPATH];
static int nfuncs;
static FILE* thunks;

static void die(const char* a, const char* b)
{
	fprintf(stderr, "ovlprep: %s%s\n", a, b);
	exit(1);
}

static int isword(int c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '.' || (c >= '0' && c <= '9');
}

static int isstart(int c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '.';
}

static int isspc(int c)
{
	return c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v';
}

static void reset(void)
{
	int i;

	poolpos = 0;
	nsym = 0;
	nfunc = 0;
	nesc = 0;
	for (i = 0; i < HASHSIZE; i++)
		table[i] = -1;
}

static unsigned hashof(const char* s, int n)
{
	unsigned h = 0;

	while (n-- > 0)
		h = h * 31 + (unsigned char)*s++;
	return h % HASHSIZE;
}

/* the index of the name of n characters, or -1 */
static int find(const char* s, int n)
{
	int i;

	for (i = table[hashof(s, n)]; i >= 0; i = syms[i].next)
		if (strncmp(syms[i].name, s, n) == 0 && syms[i].name[n] == 0)
			return i;
	return -1;
}

static int intern(const char* s, int n)
{
	int i = find(s, n);
	unsigned h;

	if (i >= 0)
		return i;
	if (nsym >= MAXSYM || poolpos + n + 1 > POOLSIZE)
		die("too many names in ", base);
	h = hashof(s, n);
	syms[nsym].name = pool + poolpos;
	memcpy(pool + poolpos, s, n);
	pool[poolpos + n] = 0;
	poolpos += n + 1;
	syms[nsym].flags = 0;
	syms[nsym].next = table[h];
	table[h] = nsym;
	return nsym++;
}

/* the length of the identifier at s ([A-Za-z_.][A-Za-z_.0-9]*), or 0 */
static int identlen(const char* s)
{
	int n = 0;

	if (!isstart(*s))
		return 0;
	while (isword(s[n]))
		n++;
	return n;
}

/* ^\.extern\s+NAME\s*$ : the name or 0 (its length in *n) */
static const char* externname(const char* s, int* n)
{
	if (strncmp(s, ".extern", 7) != 0 || !isspc(s[7]))
		return 0;
	s += 7;
	while (isspc(*s))
		s++;
	*n = identlen(s);
	if (*n == 0)
		return 0;
	if (s[*n] != 0)
	{
		const char* e = s + *n;

		while (isspc(*e))
			e++;
		if (*e != 0)
			return 0;
	}
	return s;
}

/* ^\.sect\s+(\.\w+) : the section name, in sect (0 if not) */
static int sectname(const char* s, char* sect)
{
	int n = 0;

	if (strncmp(s, ".sect", 5) != 0 || !isspc(s[5]))
		return 0;
	s += 5;
	while (isspc(*s))
		s++;
	if (*s != '.')
		return 0;
	sect[n++] = *s++;
	while (isword(*s) && *s != '.' && n < 30)
		sect[n++] = *s++;
	sect[n] = 0;
	return 1;
}

/* ^NAME: : the length of the name, or 0 */
static int labelname(const char* s)
{
	int n = identlen(s);

	return n > 0 && s[n] == ':' ? n : 0;
}

/* ^(\s*)calla(\s+)NAME(\s*)$ : the offset and length of the name, or -1 */
static int callname(const char* s, int* len)
{
	int i = 0;
	int n;

	while (isspc(s[i]))
		i++;
	if (strncmp(s + i, "calla", 5) != 0)
		return -1;
	i += 5;
	if (!isspc(s[i]))
		return -1;
	while (isspc(s[i]))
		i++;
	n = identlen(s + i);
	if (n == 0)
		return -1;
	{
		const char* e = s + i + n;

		while (isspc(*e))
			e++;
		if (*e != 0)
			return -1;
	}
	*len = n;
	return i;
}

/* a line of the file: 0 at the end */
static int readline(FILE* f, int* hasnl)
{
	int n;

	if (!fgets(line, sizeof line, f))
		return 0;
	n = strlen(line);
	*hasnl = n > 0 && line[n - 1] == '\n';
	if (*hasnl)
		line[n - 1] = 0;
	return 1;
}

/* the first pass: the global functions, in the order of their labels, and the names that are .extern */
static void pass1(FILE* f)
{
	char sect[32];
	const char* e;
	int n, i, hasnl;
	int intext = 0;
	int prevnl = 1;

	while (readline(f, &hasnl))
	{
		if (!prevnl)
		{
			prevnl = hasnl;
			continue;
		}
		prevnl = hasnl;
		e = externname(line, &n);
		if (e)
		{
			i = intern(e, n);
			syms[i].flags |= F_EXT;
			continue;
		}
		if (sectname(line, sect))
		{
			intext = strcmp(sect, ".text") == 0;
			continue;
		}
		n = labelname(line);
		if (n > 0 && intext && line[0] == '_')
		{
			i = intern(line, n);
			if (!(syms[i].flags & F_FUNC))
			{
				syms[i].flags |= F_FUNC;
				funcorder[nfunc++] = i;
			}
		}
	}
}

/* the second pass: the static functions that are used other than by calla */
static void pass2(FILE* f)
{
	int hasnl, n, i, prevnl = 1, len;
	const char* p;

	while (readline(f, &hasnl))
	{
		if (!prevnl)
		{
			prevnl = hasnl;
			continue;
		}
		prevnl = hasnl;
		if (strncmp(line, ".sect", 5) == 0 || strncmp(line, ".extern", 7) == 0 || labelname(line) > 0)
			continue;
		if (callname(line, &len) >= 0)
			continue;
		for (p = line; *p;)
		{
			n = identlen(p);
			if (n == 0)
			{
				p++;
				continue;
			}
			i = find(p, n);
			if (i >= 0 && (syms[i].flags & (F_FUNC | F_EXT)) == F_FUNC && !(syms[i].flags & F_ESC))
			{
				syms[i].flags |= F_ESC;
				escorder[nesc++] = i;
			}
			p += n;
		}
	}
}

/* the line with the names of the escaped static functions renamed, in ren */
static void rename_tokens(const char* s)
{
	int n = 0, k, i, l;
	int pos = 0;

	while (s[pos])
	{
		if (isword(s[pos]))
		{
			k = pos;
			while (isword(s[k]))
				k++;
			i = find(s + pos, k - pos);
			if (i >= 0 && (syms[i].flags & F_ESC))
			{
				l = strlen(syms[newname[i]].name);
				if (n + l >= RENMAX - 1)
					die("line too long in ", base);
				memcpy(ren + n, syms[newname[i]].name, l);
				n += l;
			}
			else
			{
				if (n + k - pos >= RENMAX - 1)
					die("line too long in ", base);
				memcpy(ren + n, s + pos, k - pos);
				n += k - pos;
			}
			pos = k;
		}
		else
		{
			if (n >= RENMAX - 1)
				die("line too long in ", base);
			ren[n++] = s[pos++];
		}
	}
	ren[n] = 0;
}

/* a line of the file of the overlay k, written the way rewrite() of the Python does */
static void rewrite(FILE* out, const char* s, int k, int* declared)
{
	char sect[32];
	const char* e;
	int n, i, off, l;

	if (strncmp(s, header, sizeof header - 1) == 0 && !*declared)
	{
		fputs(header, out);
		fputs("; .sect .end", out);
		for (i = 0; i <= k; i++)
			fprintf(out, "; .sect .ovl%d", i);
		fputs(s + sizeof header - 1, out);
		*declared = 1;
		return;
	}
	if (sectname(s, sect) && strcmp(sect, ".text") == 0 && strncmp(s, header, sizeof header - 1) != 0)
	{
		fprintf(out, ".sect .ovl%d", k);
		return;
	}
	e = externname(s, &n);
	if (e)
	{
		i = find(e, n);
		if (i >= 0 && (syms[i].flags & F_MINE))
		{
			fprintf(out, ".extern %s.o%d", syms[i].name, k);
			return;
		}
	}
	n = labelname(s);
	if (n > 0)
	{
		i = find(s, n);
		if (i >= 0 && (syms[i].flags & F_MINE))
		{
			fprintf(out, "%s.o%d:%s", syms[i].name, k, s + n + 1);
			return;
		}
	}
	off = callname(s, &l);
	if (off >= 0)
	{
		i = find(s + off, l);
		if (i >= 0 && (syms[i].flags & F_MINE))
		{
			fwrite(s, 1, off + l, out);
			fprintf(out, ".o%d%s", k, s + off + l);
			return;
		}
	}
	fputs(s, out);
}

static void stub(const char* name, int k)
{
	fprintf(thunks, ".extern %s\n.align 2\n%s:\n\ttrap $5\n.data2 %d\n.data2 %s.o%d\n.data2 __ovlret\n", name, name, k, name, k);
	nfuncs++;
}

/* the file path of overlay k, copied to outdir */
static void file(const char* path, const char* outdir, int k)
{
	FILE* f = fopen(path, "r");
	FILE* out;
	const char* b;
	char* dot;
	int i, j, n, hasnl, declared = 0, prevnl = 1, inserted = 0;

	if (!f)
		die("cannot read ", path);
	b = strrchr(path, '/');
	b = b ? b + 1 : path;
	if (strlen(b) >= MAXPATH || strlen(outdir) + strlen(b) + 2 > sizeof tmp)
		die("the name is too long: ", path);
	strcpy(base, b);
	dot = strrchr(base, '.');
	if (dot)
		*dot = 0;
	reset();
	pass1(f);
	for (i = 0; i < nfunc; i++)
		if (!(syms[funcorder[i]].flags & F_EXT))
			;
	rewind(f);
	pass2(f);
	rewind(f);
	/* the names of the promoted functions, and who is mine */
	for (i = 0; i < nesc; i++)
	{
		char nm[2 * MAXPATH + 2];

		sprintf(nm, "%s.%s", syms[escorder[i]].name, base);
		newname[escorder[i]] = intern(nm, strlen(nm));
		syms[newname[escorder[i]]].flags |= F_MINE;
	}
	for (i = 0; i < nfunc; i++)
		if ((syms[funcorder[i]].flags & F_EXT) && !(syms[funcorder[i]].flags & F_ESC))
		{
			syms[funcorder[i]].flags |= F_MINE;
			stub(syms[funcorder[i]].name, k);
		}
	for (i = 0; i < nesc; i++)
		stub(syms[newname[escorder[i]]].name, k);
	sprintf(tmp, "%s/%s", outdir, b);
	out = fopen(tmp, "w");
	if (!out)
		die("cannot create ", tmp);
	while (fgets(line, sizeof line, f))
	{
		n = strlen(line);
		hasnl = n > 0 && line[n - 1] == '\n';
		if (hasnl)
			line[n - 1] = 0;
		if (!prevnl)
		{
			fputs(line, out);
			if (hasnl)
				putc('\n', out);
			prevnl = hasnl;
			continue;
		}
		prevnl = hasnl;
		if (nesc > 0)
			rename_tokens(line);
		else
			strcpy(ren, line);
		rewrite(out, ren, k, &declared);
		if (hasnl)
			putc('\n', out);
		if (nesc > 0 && !inserted && strncmp(ren, header, sizeof header - 1) == 0)
		{
			inserted = 1;
			for (j = 0; j < nesc; j++)
			{
				char ext[2 * MAXPATH + 16];

				sprintf(ext, ".extern %s", syms[newname[escorder[j]]].name);
				rewrite(out, ext, k, &declared);
				putc('\n', out);
			}
		}
	}
	fclose(out);
	fclose(f);
}

int main(int argc, char** argv)
{
	const char* outdir = 0;
	const char* thunkfile = 0;
	char path[MAXPATH + 32];
	int ngroups = 0;
	int incur = 0;
	int i, k = -1;
	int nfiles = 0;
	struct { int start; } dummy;

	(void)dummy;
	for (i = 1; i < argc; i++)
	{
		if (strcmp(argv[i], "-o") == 0 && i + 1 < argc)
			outdir = argv[++i];
		else if (strcmp(argv[i], "-t") == 0 && i + 1 < argc)
			thunkfile = argv[++i];
		else
			break;
	}
	if (!outdir || i >= argc)
		die("usage: ovlprep -o outdir [-t thunks.s] group file.s ... + group file.s ...", "");
	/* count the groups */
	{
		int j;

		for (j = i, incur = 0; j < argc; j++)
		{
			if (strcmp(argv[j], "+") == 0)
				incur = 0;
			else if (!incur)
			{
				incur = 1;
				ngroups++;
			}
		}
	}
	if (ngroups < 2)
		die("at least two overlays: with one the code fits in the 64 KiB", "");
	if (!thunkfile)
	{
		sprintf(path, "%s/ovl_thunks.s", outdir);
		thunkfile = path;
	}
	thunks = fopen(thunkfile, "w");
	if (!thunks)
		die("cannot create ", thunkfile);
	fprintf(thunks, "%s\n.sect .text\n.align 2\n__ovlret:\n\ttrap $6\n", header);
	incur = 0;
	for (; i < argc; i++)
	{
		if (strcmp(argv[i], "+") == 0)
			incur = 0;
		else if (!incur)
		{
			incur = 1;
			k++;
		}
		else
		{
			file(argv[i], outdir, k);
			nfiles++;
		}
	}
	fclose(thunks);
	fprintf(stderr, "%d functions in %d overlays\n", nfuncs, ngroups);
	return 0;
}
