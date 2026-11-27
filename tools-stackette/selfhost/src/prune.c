/*
 * prune: drop declarations that nothing refers to from the header parts of
 * a preprocessed C file.
 *
 *	prune in.i out.i
 *
 * Everything that comes from the file named by the first linemarker (the
 * main file) is kept. A declaration from any other file is kept only when
 * one of the names it declares is used by the main file or by a declaration
 * that is kept. Names are compared by a 15 bit hash, so a clash can only keep
 * a declaration that could have been dropped.
 *
 * The input is read several times instead of being held in memory: the first
 * pass collects the names used by the main file, then the declarations are
 * visited until no new one is kept, and the last pass writes the result.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LINEMAX 2048
#define MAXTOK 400
#define MAXDECL 64
#define MAXITEMS 8192
#define HASHBITS 32768
#define NAMELEN 200

#define K_NONE 0
#define K_STRUCT 1
#define K_UNION 2
#define K_ENUM 3
#define K_TYPEDEF 4
#define K_TYPE 5

#define T_IDENT 1
#define T_OTHER 2
#define T_PUNCT 3

#define MODE_MAIN 0
#define MODE_SCAN 1
#define MODE_OUT 2

struct tok {
	unsigned short h;
	char kind;
	char kw;
	char c;
};

static unsigned char used[HASHBITS / 8];
static unsigned char keep[MAXITEMS / 8];
static struct tok toks[MAXTOK];
static int ntok;
static int overflow;
static unsigned short decl[MAXDECL];
static int ndecl;
static int declover;

/* state of the item being read */
static int depth;
static int body;
static int ended;
static int prevc;
static int mode;

static char mainfile[NAMELEN];
static char curfile[NAMELEN];
static char line[LINEMAX];

static FILE* in;
static FILE* out;

static const char* const keywords[] = {
	"struct", "union", "enum", "typedef",
	"int", "char", "short", "long", "unsigned", "signed", "float", "double",
	"void", "const", "volatile", "static", "extern", "register", "auto",
	"sizeof", 0
};

static int keyword(const char* s)
{
	int i;

	for (i = 0; keywords[i]; i++)
		if (strcmp(keywords[i], s) == 0)
			return i < 4 ? i + 1 : K_TYPE;
	return K_NONE;
}

static int getbit(unsigned char* a, unsigned n)
{
	return (a[n >> 3] >> (n & 7)) & 1;
}

static void setbit(unsigned char* a, unsigned n)
{
	a[n >> 3] |= 1 << (n & 7);
}

static int isid1(int c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static int isdig(int c)
{
	return c >= '0' && c <= '9';
}

static int isid(int c)
{
	return isid1(c) || isdig(c);
}

static int ispace(int c)
{
	return c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v' || c == '\n';
}

static unsigned hashof(const char* s)
{
	unsigned h = 0;

	while (*s)
		h = (h * 31 + (unsigned char)*s++) & (HASHBITS - 1);
	return h;
}

static int indecl(unsigned h)
{
	int i;

	for (i = 0; i < ndecl; i++)
		if (decl[i] == h)
			return 1;
	return 0;
}

static void adddecl(unsigned h)
{
	if (indecl(h))
		return;
	if (ndecl < MAXDECL)
		decl[ndecl++] = h;
	else
		declover = 1;
}

static int ispun(int i, int c)
{
	return i >= 0 && i < ntok && toks[i].kind == T_PUNCT && toks[i].c == c;
}

static int isenumbrace(int i)
{
	int j;
	int d = 0;

	for (j = i; j >= 0; j--)
	{
		if (ispun(j, '}'))
			d++;
		else if (ispun(j, '{'))
		{
			if (d == 0)
				return (j >= 2 && toks[j - 2].kw == K_ENUM) || (j >= 1 && toks[j - 1].kw == K_ENUM);
			d--;
		}
	}
	return 0;
}

static int istag(int i)
{
	return i >= 0 && (toks[i].kw == K_STRUCT || toks[i].kw == K_UNION || toks[i].kw == K_ENUM);
}

static void declared(void)
{
	int i;
	int brace = 0;
	int paren = 0;
	int next;

	ndecl = 0;
	declover = 0;
	for (i = 0; i < ntok; i++)
	{
		if (toks[i].kind == T_PUNCT)
		{
			switch (toks[i].c)
			{
				case '{':
					brace++;
					if (paren == 0 && brace == 1 && i > 1 && toks[i - 1].kind == T_IDENT && istag(i - 2))
						adddecl(toks[i - 1].h);
					break;
				case '}':
					brace--;
					break;
				case '(':
					paren++;
					break;
				case ')':
					paren--;
					break;
			}
			continue;
		}
		if (toks[i].kind != T_IDENT)
			continue;
		if (istag(i - 1))
		{
			adddecl(toks[i].h);
			continue;
		}
		if (toks[i].kw != K_NONE)
			continue;
		if (brace == 1 && isenumbrace(i))
		{
			if (ispun(i + 1, ',') || ispun(i + 1, '}') || ispun(i + 1, '='))
				adddecl(toks[i].h);
			continue;
		}
		if (brace > 0)
			continue;
		next = i + 1;
		if (paren == 0 && (ispun(next, ';') || ispun(next, ',') || ispun(next, '[') || ispun(next, '(') || ispun(next, '=')))
			adddecl(toks[i].h);
		else if (paren == 1 && ispun(i - 1, '*') && ispun(i - 2, '(') && ispun(next, ')'))
			adddecl(toks[i].h);
	}
}

static void markrefs(void)
{
	int i;

	for (i = 0; i < ntok; i++)
		if (toks[i].kind == T_IDENT && !indecl(toks[i].h))
			setbit(used, toks[i].h);
}

/* one token of a header item */
static void addtok(int kind, int kw, int c, unsigned h)
{
	if (mode == MODE_SCAN)
	{
		if (ntok < MAXTOK)
		{
			toks[ntok].kind = kind;
			toks[ntok].kw = kw;
			toks[ntok].c = c;
			toks[ntok].h = h;
			ntok++;
		}
		else
		{
			overflow = 1;
			if (kind == T_IDENT)
				setbit(used, h);
		}
	}
	if (kind == T_PUNCT)
	{
		if (c == '{')
		{
			if (depth == 0 && prevc == ')')
				body = 1;
			depth++;
		}
		else if (c == '}')
		{
			depth--;
			if (depth == 0 && body)
				ended = 1;
		}
		else if (c == ';' && depth == 0)
			ended = 1;
		prevc = c;
	}
	else
		prevc = 0;
}

/* tokens of a line; with all set, only the names are of interest */
static void scan(const char* p, int all)
{
	char word[NAMELEN];
	int n;
	int c;
	int q;

	while (*p && (all || !ended))
	{
		c = *p;
		if (ispace(c))
		{
			p++;
			continue;
		}
		if (isid1(c))
		{
			n = 0;
			while (isid(*p))
			{
				if (n < NAMELEN - 1)
					word[n++] = *p;
				p++;
			}
			word[n] = 0;
			if (all)
				setbit(used, hashof(word));
			else
				addtok(T_IDENT, keyword(word), 0, hashof(word));
			continue;
		}
		if (isdig(c))
		{
			while (isid(*p) || *p == '.')
				p++;
			if (!all)
				addtok(T_OTHER, 0, 0, 0);
			continue;
		}
		if (c == '"' || c == '\'')
		{
			q = c;
			p++;
			while (*p && *p != q)
			{
				if (*p == '\\' && p[1])
					p++;
				p++;
			}
			if (*p)
				p++;
			if (!all)
				addtok(T_OTHER, 0, 0, 0);
			continue;
		}
		p++;
		if (!all && c != '#')
			addtok(T_PUNCT, 0, c, 0);
	}
}

/* '# 12 "file"': returns the line number or -1 */
static long marker(const char* s, char* name)
{
	long n = 0;
	int i;

	if (*s++ != '#')
		return -1;
	while (*s == ' ' || *s == '\t')
		s++;
	if (!isdig(*s))
		return -1;
	while (isdig(*s))
		n = n * 10 + *s++ - '0';
	while (*s == ' ' || *s == '\t')
		s++;
	if (*s++ != '"')
		return -1;
	for (i = 0; *s && *s != '"' && i < NAMELEN - 1; i++)
		name[i] = *s++;
	if (*s != '"')
		return -1;
	name[i] = 0;
	return n;
}

static int blank(const char* s)
{
	while (*s)
		if (!ispace(*s++))
			return 0;
	return 1;
}

/* decide about the item that has just ended; returns 1 if it is newly kept */
static int finish(int itemno, int forced)
{
	int i;

	declared();
	if (itemno >= MAXITEMS)
	{
		markrefs();
		return 0;
	}
	if (getbit(keep, itemno))
		return 0;
	if (!(forced || overflow || declover || ndecl == 0))
	{
		for (i = 0; i < ndecl; i++)
			if (getbit(used, decl[i]))
				break;
		if (i == ndecl)
			return 0;
	}
	setbit(keep, itemno);
	markrefs();
	return 1;
}

static int pass(int m)
{
	long lineno = 1;
	long lastmain = -2;
	long ln;
	char name[NAMELEN];
	int itemno = -1;
	int active = 0;
	int forced = 0;
	int emit = 0;
	int changed = 0;
	int newline = 1;
	int hasmain = 0;
	int eol;
	int inmain;
	size_t len;

	mode = m;
	rewind(in);
	curfile[0] = 0;
	while (fgets(line, sizeof line, in))
	{
		len = strlen(line);
		eol = len > 0 && line[len - 1] == '\n';
		if (newline && line[0] == '#' && (ln = marker(line, name)) >= 0)
		{
			strcpy(curfile, name);
			if (!hasmain)
			{
				strcpy(mainfile, name);
				hasmain = 1;
			}
			lineno = ln;
			newline = eol;
			continue;
		}
		inmain = strcmp(curfile, mainfile) == 0;
		if (inmain && !active)
		{
			if (!(newline && blank(line)))
			{
				if (mode == MODE_OUT)
				{
					if (newline && lastmain != lineno - 1)
						fprintf(out, "# %ld \"%s\"\n", lineno, mainfile);
					fputs(line, out);
				}
				else if (mode == MODE_MAIN)
					scan(line, 1);
				if (newline)
					lastmain = lineno;
			}
		}
		else if (mode != MODE_MAIN)
		{
			if (!active)
			{
				if (blank(line))
					goto next;
				active = 1;
				ended = 0;
				depth = 0;
				body = 0;
				prevc = 0;
				forced = 0;
				ntok = 0;
				overflow = 0;
				itemno++;
				if (mode == MODE_OUT)
				{
					emit = itemno >= MAXITEMS || getbit(keep, itemno);
					if (emit)
					{
						fprintf(out, "# %ld \"%s\"\n", lineno, curfile);
						lastmain = -2;
					}
				}
			}
			if (mode == MODE_OUT && emit)
				fputs(line, out);
			if (inmain)
				forced = 1;
			else
				scan(line, 0);
			if (ended)
			{
				active = 0;
				if (mode == MODE_SCAN && finish(itemno, forced))
					changed = 1;
			}
		}
	next:
		if (eol)
			lineno++;
		newline = eol;
	}
	if (active && mode == MODE_SCAN && finish(itemno, forced))
		changed = 1;
	return changed;
}

int main(int argc, char** argv)
{
	if (argc != 3)
	{
		fputs("usage: prune in.i out.i\n", stderr);
		return 1;
	}
	in = fopen(argv[1], "r");
	if (!in)
	{
		fputs("prune: cannot open input\n", stderr);
		return 1;
	}
	out = fopen(argv[2], "w");
	if (!out)
	{
		fputs("prune: cannot create output\n", stderr);
		return 1;
	}
	pass(MODE_MAIN);
	while (pass(MODE_SCAN))
		;
	pass(MODE_OUT);
	fclose(out);
	return 0;
}
