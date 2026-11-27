/*
 * ack2minix: turns an ACK output file linked as a separate I&D program
 * (em_led -b1:0) into a Minix a.out executable. See tools-stackette/ack2minix.py,
 * of which this is the version for the target (without overlays).
 *
 *	ack2minix prog.out -o prog [--stack BYTES] [--heap BYTES]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>

#define O_MAGIC 0x0202
#define HEADLEN 20
#define SECTLEN 20
#define MAXSECT 16
#define TEXT 0
#define ROM 1
#define DATA 2
#define BSS 3
#define OVL_ROOT 0xc000L
#define PAGE 4096L

struct sect {
	unsigned long base, size, foff, flen;
};

static FILE* fi;
static FILE* fo;
static unsigned char buf[1024];

static unsigned long rd(const unsigned char* p, int n)
{
	unsigned long v = 0;

	while (n--)
		v = (v << 8) | p[n];
	return v;
}

static void wr(unsigned long v, int n)
{
	while (n--)
	{
		putc((int)(v & 0xff), fo);
		v >>= 8;
	}
}

static void die(const char* s)
{
	fprintf(stderr, "ack2minix: %s\n", s);
	exit(1);
}

static void zeros(unsigned long n)
{
	while (n--)
		putc(0, fo);
}

/* the contents of a section: the file part, then zeros up to its size */
static void copy(struct sect* s)
{
	unsigned long left = s->flen;
	size_t n;

	if (fseek(fi, (long)s->foff, 0) != 0)
		die("seek");
	while (left > 0)
	{
		n = left > sizeof buf ? sizeof buf : (size_t)left;
		if (fread(buf, 1, n, fi) != n)
			die("short input");
		fwrite(buf, 1, n, fo);
		left -= n;
	}
	zeros(s->size - s->flen);
}

int main(int argc, char** argv)
{
	struct sect s[MAXSECT];
	unsigned char h[HEADLEN];
	unsigned char e[SECTLEN];
	const char* inname = 0;
	const char* outname = 0;
	unsigned long stack = 0x2000;
	unsigned long heap = 0;
	unsigned long textlen, image, total;
	int nsect, i;

	for (i = 1; i < argc; i++)
	{
		if (strcmp(argv[i], "-o") == 0 && i + 1 < argc)
			outname = argv[++i];
		else if (strcmp(argv[i], "--stack") == 0 && i + 1 < argc)
			stack = strtoul(argv[++i], 0, 0);
		else if (strcmp(argv[i], "--heap") == 0 && i + 1 < argc)
			heap = strtoul(argv[++i], 0, 0);
		else if (!inname)
			inname = argv[i];
		else
			die("usage: ack2minix prog.out -o prog [--stack N] [--heap N]");
	}
	if (!inname || !outname)
		die("usage: ack2minix prog.out -o prog [--stack N] [--heap N]");
	fi = fopen(inname, "rb");
	if (!fi)
		die("cannot open the input");
	if (fread(h, 1, HEADLEN, fi) != HEADLEN)
		die("short input");
	nsect = (int)rd(h + 6, 2);
	if (rd(h, 2) != O_MAGIC || nsect < 4)
		die("not an ACK output file with four sections");
	if (nsect > MAXSECT)
		die("too many sections");
	for (i = 0; i < nsect; i++)
	{
		if (fseek(fi, (long)(HEADLEN + SECTLEN * i), 0) != 0 || fread(e, 1, SECTLEN, fi) != SECTLEN)
			die("short input");
		s[i].base = rd(e, 4);
		s[i].size = rd(e + 4, 4);
		s[i].foff = rd(e + 8, 4);
		s[i].flen = rd(e + 12, 4);
		if (i >= 4 && s[i].base == OVL_ROOT)
			die("overlays are not supported here");
	}
	if (s[TEXT].base != 0 || s[ROM].base != 0)
		die("link with -b1:0: the text and the rom must start at 0");
	if (s[DATA].base != s[ROM].size)
		die("the data does not follow the rom");
	if (s[BSS].base != s[DATA].base + s[DATA].size)
		die("the bss does not follow the data");
	textlen = (s[TEXT].size + 1) & ~1UL;
	if (textlen > 0x10000L)
		die("the code does not fit in 64 KiB");
	image = s[ROM].size + s[DATA].size;
	total = (image + s[BSS].size + heap + stack + PAGE - 1) / PAGE * PAGE;
	if (total > 0x10000L)
		die("the data segment does not fit in 64 KiB");
	fo = fopen(outname, "wb");
	if (!fo)
		die("cannot create the output");
	putc(0x01, fo);
	putc(0x03, fo);
	putc(0x30, fo);
	putc(0x20, fo);
	putc(32, fo);
	putc(0, fo);
	wr(0, 2);
	wr(textlen, 4);
	wr(image, 4);
	wr(s[BSS].size, 4);
	wr(0, 4);
	wr(total, 4);
	wr(0, 4);
	copy(&s[TEXT]);
	zeros(textlen - s[TEXT].size);
	copy(&s[ROM]);
	copy(&s[DATA]);
	fclose(fo);
	chmod(outname, 0755);
	fprintf(stderr, "text %lu, data %lu, bss %lu, total %lu\n", textlen, image, s[BSS].size, total);
	return 0;
}
