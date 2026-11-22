/* hmtest: the data window and the handles of <hmem.h>.
 *
 * It keeps 100 KB in a pool through a window of 4 pages (16 KB), checks the
 * contents, a fork (the child has a pool of its own), and read() and write()
 * through the window (the kernel's umap). Prints "hmtest: ok" or what failed.
 */
#include <sys/types.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <hmem.h>

#define POOL	300	/* pages */
#define BIG	(100L * 1024)

static void say(s) char *s;
{
  (void) write(1, s, strlen(s));
}

static void fail(s) char *s;
{
  say("hmtest: FAILED: ");
  say(s);
  say("\n");
  _exit(1);
}

static unsigned char pat(seed, off)
unsigned seed;
unsigned long off;
{
  return (unsigned char) (seed * 31 + (unsigned) off * 7 + (unsigned) (off >> 8));
}

static void fill(h, seed, size)
hm_t h;
unsigned seed;
unsigned long size;
{
  unsigned long off;
  unsigned n, k;
  unsigned char *p;

  for (off = 0; off < size; off += 8192) {
	n = size - off < 8192 ? (unsigned) (size - off) : 8192;
	p = (unsigned char *) hm_lock_range(h, off, n);
	if (p == NULL) fail("lock in fill");
	for (k = 0; k < n; k++) p[k] = pat(seed, off + k);
	hm_unlock_range(h, off, n);
  }
}

static int verify(h, seed, size, step)
hm_t h;
unsigned seed;
unsigned long size;
unsigned step;
{
  unsigned long off;
  unsigned n, k;
  unsigned char *p;

  for (off = 0; off < size; off += step) {
	n = size - off < step ? (unsigned) (size - off) : step;
	p = (unsigned char *) hm_lock_range(h, off, n);
	if (p == NULL) fail("lock in verify");
	for (k = 0; k < n; k++)
		if (p[k] != pat(seed, off + k)) { hm_unlock_range(h, off, n); return(0); }
	hm_unlock_range(h, off, n);
  }
  return(1);
}

int main()
{
  hm_t big, a, b;
  int fd, pid, st;
  unsigned char *p;
  unsigned k;

  if (hm_init(4, POOL) < 0) {
	say(errno == ENOMEM ? "hm_init: ENOMEM\n" : errno == EINVAL ? "hm_init: EINVAL\n" : "hm_init: other\n");
	fail("hm_init");
  }
  say("pool ready\n");
  big = hm_alloc(BIG);
  if (big == 0) fail("hm_alloc 100K");
  fill(big, 5, BIG);
  say("100K written\n");
  if (!verify(big, 5, BIG, 8192)) fail("100K verify");
  say("100K ok\n");

  /* more blocks, free, realloc */
  a = hm_alloc(10000L);
  b = hm_alloc(30000L);
  if (a == 0 || b == 0) fail("alloc a, b");
  fill(a, 1, 10000L);
  fill(b, 2, 30000L);
  if (hm_realloc(a, 20000L) != a) fail("realloc grow");
  if (!verify(a, 1, 10000L, 4096)) fail("realloc kept the contents");
  if (hm_realloc(b, 5000L) != b) fail("realloc shrink");
  if (!verify(b, 2, 5000L, 4096)) fail("shrink kept the contents");
  hm_free(b);
  if (hm_lock(a) != NULL) fail("lock of 20000 bytes in 16K window");
  say("blocks ok\n");

  /* a fork: the child has a copy of the pool */
  pid = fork();
  if (pid < 0) fail("fork");
  if (pid == 0) {
	if (!verify(big, 5, BIG, 8192)) fail("child sees the pool");
	fill(big, 9, 8192L);
	if (!verify(big, 9, 8192L, 4096)) fail("child writes the pool");
	_exit(0);
  }
  if (wait(&st) != pid || st != 0) fail("child failed");
  if (!verify(big, 5, BIG, 8192)) fail("the child changed the pool of the parent");
  say("fork ok\n");

  /* write() and read() with the window as the buffer */
  fd = creat("/tmp/hm.dat", 0644);
  if (fd < 0) fail("creat");
  p = (unsigned char *) hm_lock_range(big, 0L, 8192);
  if (p == NULL) fail("lock for write");
  if (write(fd, (char *) p, 8192) != 8192) fail("write from the window");
  hm_unlock_range(big, 0L, 8192);
  close(fd);
  b = hm_alloc(8192L);
  if (b == 0) fail("alloc for read");
  p = (unsigned char *) hm_lock_range(b, 0L, 8192);
  fd = open("/tmp/hm.dat", O_RDONLY);
  if (fd < 0 || p == NULL) fail("open for read");
  if (read(fd, (char *) p, 8192) != 8192) fail("read into the window");
  close(fd);
  for (k = 0; k < 8192; k++)
	if (p[k] != pat(5, (unsigned long) k)) fail("contents read through the window");
  hm_unlock_range(b, 0L, 8192);
  unlink("/tmp/hm.dat");
  say("read, write ok\n");
  say("hmtest: ok\n");
  return(0);
}
