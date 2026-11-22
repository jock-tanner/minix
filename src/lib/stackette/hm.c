/* hm.c: handles on the pool of pages outside of the data space (<hmem.h>).
 *
 * The pool belongs to the memory manager (call POOL) and the kernel shows its
 * pages in a window of the data space (trap 7). Here the pool is a bitmap of
 * pages, a block is a run of them and a handle is its entry in a table. The
 * window is a few slots, and every mapping is a run of pool pages in
 * consecutive slots with a lock count. A mapping without locks stays as long
 * as nothing needs its slots (the least recently used goes first), so locking
 * a block that is still shown costs no trap.
 */
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <hmem.h>

#ifdef HM_HOST
int _hm_pool(unsigned pages, void *win, unsigned wn);
int _hm_winmap(unsigned slot, unsigned page, unsigned n);
void *_hm_sbrk(unsigned bytes);
#define sbrk _hm_sbrk
#else
#include <lib.h>
#include <unistd.h>
int _winmap(unsigned slot, unsigned page, unsigned n);
#define _hm_winmap _winmap
PRIVATE int _hm_pool(pages, win, wn)
unsigned pages;
void *win;
unsigned wn;
{
  message m;

  m.m1_i1 = pages;
  m.m1_i2 = wn;
  m.m1_p1 = (char *) win;
  return(_syscall(MM, POOL, &m));
}
#endif

#define MAX_HANDLES	256

struct blk {
  unsigned first;		/* the first page of the run, in the pool */
  unsigned npages;		/* its length, 0 if the entry is free */
  unsigned long size;		/* the size in bytes */
};

struct map {
  unsigned first, n;		/* the pool pages that are shown */
  unsigned slot;		/* ... and the first slot that shows them */
  unsigned lock;
  unsigned stamp;		/* when it was used last */
  int used;
};

static char *win;		/* the window, page aligned */
static unsigned wn, npool, nblk, stamp;
static unsigned char *bitmap;	/* a bit for each page of the pool: in a block */
static struct blk *tab;
static struct map maps[HM_MAXWIN];
static int owner[HM_MAXWIN];	/* the mapping that has the slot, or -1 */

#define PAGES(bytes)	((unsigned) (((bytes) + (HM_PAGE - 1)) >> 12))

static int used(p) unsigned p; { return(bitmap[p >> 3] & (1 << (p & 7))); }
static void mark(p, n, v) unsigned p, n; int v;
{
  while (n-- > 0) {
	if (v) bitmap[p >> 3] |= 1 << (p & 7);
	else bitmap[p >> 3] &= ~(1 << (p & 7));
	p++;
  }
}

static int find_run(n)
unsigned n;
{
/* The first run of n free pages, or -1. */
  unsigned p, run = 0;

  for (p = 0; p < npool; p++) {
	if (used(p)) run = 0;
	else if (++run == n) return(p + 1 - n);
  }
  return(-1);
}

int hm_init(win_pages, pool_pages)
unsigned win_pages, pool_pages;
{
/* Make the pool of pool_pages pages and the window of win_pages. */
  char *b;
  unsigned lo, a;

  if (wn != 0) { errno = EBUSY; return(-1); }
  if (win_pages == 0 || win_pages > HM_MAXWIN || pool_pages == 0
					|| pool_pages > 4096) {
	errno = EINVAL;
	return(-1);
  }
  b = (char *) sbrk(win_pages * HM_PAGE + (HM_PAGE - 1));
  if (b == (char *) -1) return(-1);
  lo = (unsigned) ((unsigned long) b & (HM_PAGE - 1));
  win = lo == 0 ? b : b + (HM_PAGE - lo);
  nblk = pool_pages < MAX_HANDLES ? pool_pages : MAX_HANDLES;
  bitmap = (unsigned char *) calloc((pool_pages + 7) / 8, 1);
  tab = (struct blk *) calloc(nblk, sizeof(struct blk));
  if (bitmap == NULL || tab == NULL) {
	errno = ENOMEM;
	return(-1);
  }
  if (_hm_pool(pool_pages, win, win_pages) < 0) return(-1);
  npool = pool_pages;
  wn = win_pages;
  for (a = 0; a < HM_MAXWIN; a++) owner[a] = -1;
  return(0);
}

static struct blk *getblk(h)
hm_t h;
{
  if (h == 0 || h > nblk || tab == NULL || tab[h - 1].npages == 0) return(NULL);
  return(&tab[h - 1]);
}

static void drop(m)
struct map *m;
{
  unsigned s;

  for (s = m->slot; s < m->slot + m->n; s++) owner[s] = -1;
  m->used = 0;
}

static void *lock_pages(p0, n, off)
unsigned p0, n, off;
{
/* Show the pool pages p0.. (n of them) in the window; the address of the byte
 * off of the first. */
  unsigned i, pos, s, bestpos, age, bestage;
  int found, blocked, free_all;
  struct map *m;

  if (n == 0 || n > wn) return(NULL);
  for (i = 0, m = maps; i < HM_MAXWIN; i++, m++)
	if (m->used && m->first <= p0 && p0 + n <= m->first + m->n) {
		m->lock++;
		m->stamp = ++stamp;
		return(win + (m->slot + (p0 - m->first)) * HM_PAGE + off);
	}
  /* A page is in one mapping at most, so that a lock and its unlock find
   * the same one: a mapping of some of these pages that is not locked goes,
   * and one that is locked refuses the request. */
  for (i = 0, m = maps; i < HM_MAXWIN; i++, m++)
	if (m->used && m->first < p0 + n && p0 < m->first + m->n) {
		if (m->lock != 0) { errno = EBUSY; return(NULL); }
		drop(m);
	}
  /* the place for the run: free slots, or else the ones whose newest mapping
   * was used the longest ago (the stamps may wrap, the difference is good) */
  found = 0;
  bestage = 0;
  bestpos = 0;
  for (pos = 0; pos + n <= wn; pos++) {
	blocked = 0;
	age = 0xffff;
	free_all = 1;
	for (s = pos; s < pos + n; s++) {
		if (owner[s] < 0) continue;
		free_all = 0;
		m = &maps[owner[s]];
		if (m->lock != 0) { blocked = 1; break; }
		if (stamp - m->stamp < age) age = stamp - m->stamp;
	}
	if (blocked) continue;
	if (free_all) { found = 1; bestpos = pos; break; }
	if (!found || age > bestage) { found = 1; bestage = age; bestpos = pos; }
  }
  if (!found) return(NULL);			/* all of it is locked */
  for (s = bestpos; s < bestpos + n; s++)
	if (owner[s] >= 0) drop(&maps[owner[s]]);
  for (i = 0, m = maps; i < HM_MAXWIN && m->used; i++, m++) {}
  if (i == HM_MAXWIN) return(NULL);
  if (_hm_winmap(bestpos, p0, n) != 0) { errno = EINVAL; return(NULL); }
  m->used = 1;
  m->first = p0;
  m->n = n;
  m->slot = bestpos;
  m->lock = 1;
  m->stamp = ++stamp;
  for (s = bestpos; s < bestpos + n; s++) owner[s] = (int) (m - maps);
  return(win + bestpos * HM_PAGE + off);
}

static void unlock_pages(p0, n)
unsigned p0, n;
{
  unsigned i;
  struct map *m;

  for (i = 0, m = maps; i < HM_MAXWIN; i++, m++)
	if (m->used && m->lock != 0 && m->first <= p0 && p0 + n <= m->first + m->n) {
		m->lock--;
		return;
	}
}

static int locked_in(p0, n)
unsigned p0, n;
{
/* Does a locked mapping touch the pool pages p0..p0+n-1? Evict the others. */
  unsigned i;
  struct map *m;

  for (i = 0, m = maps; i < HM_MAXWIN; i++, m++)
	if (m->used && m->first < p0 + n && p0 < m->first + m->n && m->lock != 0)
		return(1);
  for (i = 0, m = maps; i < HM_MAXWIN; i++, m++)
	if (m->used && m->first < p0 + n && p0 < m->first + m->n) drop(m);
  return(0);
}

hm_t hm_alloc(bytes)
unsigned long bytes;
{
  unsigned n, i;
  int first;

  if (wn == 0 || bytes == 0 || bytes > (unsigned long) npool * HM_PAGE) {
	errno = EINVAL;
	return(0);
  }
  n = PAGES(bytes);
  for (i = 0; i < nblk && tab[i].npages != 0; i++) {}
  if (i == nblk || (first = find_run(n)) < 0) { errno = ENOMEM; return(0); }
  mark(first, n, 1);
  tab[i].first = first;
  tab[i].npages = n;
  tab[i].size = bytes;
  return(i + 1);
}

hm_t hm_calloc(bytes)
unsigned long bytes;
{
  hm_t h = hm_alloc(bytes);
  struct blk *b;
  unsigned i;
  char *p;

  if (h == 0) return(0);
  b = getblk(h);
  for (i = 0; i < b->npages; i++) {
	p = (char *) lock_pages(b->first + i, 1, 0);
	if (p == NULL) { hm_free(h); return(0); }
	memset(p, 0, HM_PAGE);
	unlock_pages(b->first + i, 1);
  }
  return(h);
}

void hm_free(h)
hm_t h;
{
  struct blk *b = getblk(h);

  if (b == NULL || locked_in(b->first, b->npages)) return;
  mark(b->first, b->npages, 0);
  b->npages = 0;
}

unsigned long hm_size(h)
hm_t h;
{
  struct blk *b = getblk(h);

  return(b == NULL ? 0L : b->size);
}

hm_t hm_realloc(h, bytes)
hm_t h;
unsigned long bytes;
{
  struct blk *b = getblk(h);
  unsigned n, i;
  int first;
  char *src, *dst;

  if (b == NULL || bytes == 0 || bytes > (unsigned long) npool * HM_PAGE) {
	errno = EINVAL;
	return(0);
  }
  n = PAGES(bytes);
  if (n == b->npages) { b->size = bytes; return(h); }
  if (locked_in(b->first, b->npages)) { errno = EBUSY; return(0); }
  if (n < b->npages) {
	mark(b->first + n, b->npages - n, 0);
	b->npages = n;
	b->size = bytes;
	return(h);
  }
  for (i = b->npages; i < n && b->first + i < npool && !used(b->first + i); i++) {}
  if (i == n) {				/* it grows where it is */
	mark(b->first + b->npages, n - b->npages, 1);
	b->npages = n;
	b->size = bytes;
	return(h);
  }
  if (wn < 2 || (first = find_run(n)) < 0) { errno = ENOMEM; return(0); }
  for (i = 0; i < b->npages; i++) {
	src = (char *) lock_pages(b->first + i, 1, 0);
	dst = src == NULL ? NULL : (char *) lock_pages(first + i, 1, 0);
	if (dst == NULL) {
		if (src != NULL) unlock_pages(b->first + i, 1);
		errno = ENOMEM;
		return(0);
	}
	memcpy(dst, src, HM_PAGE);
	unlock_pages(first + i, 1);
	unlock_pages(b->first + i, 1);
  }
  mark(b->first, b->npages, 0);
  mark(first, n, 1);
  b->first = first;
  b->npages = n;
  b->size = bytes;
  return(h);
}

void *hm_lock_range(h, off, len)
hm_t h;
unsigned long off;
unsigned len;
{
  struct blk *b = getblk(h);
  unsigned long last;

  if (b == NULL || len == 0 || off + len > b->size) { errno = EINVAL; return(NULL); }
  last = off + len - 1;
  return(lock_pages(b->first + (unsigned) (off >> 12),
		(unsigned) (last >> 12) - (unsigned) (off >> 12) + 1,
		(unsigned) off & (HM_PAGE - 1)));
}

void hm_unlock_range(h, off, len)
hm_t h;
unsigned long off;
unsigned len;
{
  struct blk *b = getblk(h);
  unsigned long last;

  if (b == NULL || len == 0 || off + len > b->size) return;
  last = off + len - 1;
  unlock_pages(b->first + (unsigned) (off >> 12),
		(unsigned) (last >> 12) - (unsigned) (off >> 12) + 1);
}

void *hm_lock(h)
hm_t h;
{
  struct blk *b = getblk(h);

  if (b == NULL || b->size > (unsigned long) wn * HM_PAGE) { errno = EINVAL; return(NULL); }
  return(hm_lock_range(h, 0L, (unsigned) b->size));
}

void hm_unlock(h)
hm_t h;
{
  struct blk *b = getblk(h);

  if (b != NULL && b->size <= (unsigned long) wn * HM_PAGE)
	hm_unlock_range(h, 0L, (unsigned) b->size);
}

unsigned hm_free_pages()
{
  unsigned p, n = 0;

  for (p = 0; p < npool; p++) if (!used(p)) n++;
  return(n);
}
