/* iotest: sequential read speed of a disk device.
 *
 *   iotest [device [KiB [chunk]]]
 *
 * Reads KiB kilobytes (default 2048) from the device (default /dev/hd1) in
 * chunks of chunk kilobytes (default 8) and prints the time and the speed. It
 * only reads. The time is in clock ticks (60 per second).
 */
#include <sys/types.h>
#include <sys/times.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#define MAXCHUNK 8192

static char buf[MAXCHUNK];

static void num(v) long v;
{
  char c[12];
  int i;

  i = 12;
  do {
	c[--i] = '0' + (int) (v % 10);
	v /= 10;
  } while (v > 0);
  write(1, c + i, 12 - i);
}

static void str(s) char *s;
{
  write(1, s, strlen(s));
}

int main(argc, argv) int argc; char **argv;
{
  char *dev;
  long kib, chunk, done, t0, t1, ticks, s0, s1;
  struct tms tm;
  int fd, n;

  dev = argc > 1 ? argv[1] : "/dev/hd1";
  kib = argc > 2 ? atol(argv[2]) : 2048L;
  chunk = argc > 3 ? atol(argv[3]) : 8L;
  if (chunk < 1 || chunk > 8) chunk = 8;
  fd = open(dev, O_RDONLY);
  if (fd < 0) {
	str("iotest: cannot open ");
	str(dev);
	str("\n");
	return 1;
  }
  done = 0;
  s0 = (long) time((time_t *) 0);
  t0 = (long) times(&tm);
  while (done < kib) {
	n = read(fd, buf, (unsigned) (chunk * 1024L));
	if (n <= 0) break;
	done += n / 1024;
  }
  t1 = (long) times(&tm);
  s1 = (long) time((time_t *) 0);
  ticks = t1 - t0;
  str("read ");
  num(done);
  str(" KiB in ");
  num(ticks);
  str(" ticks, ");
  num(s1 - s0);
  str(" s by the clock");
  if (ticks > 0) {
	str(", ");
	num(done * 60L / ticks);
	str(" KiB/s if a tick is 1/60 s");
  }
  str("\n");
  return 0;
}
