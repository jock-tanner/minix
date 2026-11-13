/* ethtest: raw ethernet through /dev/eth0.
 *
 *   ethtest           send one broadcast frame (ethertype 0x1234)
 *   ethtest r         send it, then read one frame (the first is the copy of
 *                     our own broadcast that inet loops back)
 *   ethtest l [n]     only listen: print n frames (default: until killed)
 *   ethtest p [n]     the same with the promiscuous mode (all the frames)
 *
 * A frame is printed as: length, destination, source, ethertype, 16 bytes of
 * the payload. Without the promiscuous mode the frames for our address and the
 * broadcasts arrive.
 */
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <net/gen/ether.h>
#include <net/gen/eth_io.h>
#include <net/ioctl.h>

static void hex(v, n) unsigned v; int n;
{
  char c[4];
  int i;

  for (i = 0; i < n; i++) c[i] = "0123456789abcdef"[(v >> (4 * (n - 1 - i))) & 15];
  write(1, c, n);
}

static void str(s) char *s;
{
  write(1, s, strlen(s));
}

static void mac(p) unsigned char *p;
{
  int i;

  for (i = 0; i < 6; i++) {
	if (i) str(":");
	hex(p[i], 2);
  }
}

static void show(r, n) unsigned char *r; int n;
{
  int i;

  hex(n, 4);
  str(" ");
  if (n < 14) { str("short\n"); return; }
  mac(r); str(" <- "); mac(r + 6);
  str(" type "); hex(r[12] << 8 | r[13], 4);
  str(":");
  for (i = 14; i < 30 && i < n; i++) { str(" "); hex(r[i], 2); }
  str("\n");
}

int main(argc, argv) int argc; char **argv;
{
  int fd, i, n, count, listen, promisc;
  nwio_ethopt_t opt;
  nwio_ethstat_t st;
  unsigned char f[64], r[1514];

  listen = argc > 1 && (argv[1][0] == 'l' || argv[1][0] == 'p');
  promisc = argc > 1 && argv[1][0] == 'p';
  count = argc > 2 ? atoi(argv[2]) : 0;

  fd = open("/dev/eth0", O_RDWR);
  if (fd < 0) { str("open failed\n"); return(1); }
  opt.nweo_flags = NWEO_COPY | NWEO_EN_LOC | NWEO_EN_BROAD | NWEO_REMANY |
					NWEO_TYPEANY | NWEO_RWDATALL;
  if (promisc) opt.nweo_flags |= NWEO_EN_PROMISC;
  if (ioctl(fd, NWIOSETHOPT, &opt) < 0) { str("sethopt failed\n"); return(1); }
  if (ioctl(fd, NWIOGETHSTAT, &st) < 0) { str("gethstat failed\n"); return(1); }
  str("addr "); mac(st.nwes_addr.ea_addr); str("\n");

  if (listen) {
	for (i = 0; count == 0 || i < count; i++) {
		n = read(fd, r, sizeof(r));
		if (n < 0) { str("read failed\n"); return(1); }
		show(r, n);
	}
	return(0);
  }

  for (i = 0; i < 6; i++) f[i] = 0xff;
  memcpy(f + 6, st.nwes_addr.ea_addr, 6);
  f[12] = 0x12; f[13] = 0x34;
  for (i = 14; i < 60; i++) f[i] = i;
  n = write(fd, f, 60);
  str("wrote "); hex(n, 4); str("\n");
  if (argc > 1) {
	n = read(fd, r, sizeof(r));
	str("read "); show(r, n);
  }
  return(0);
}
