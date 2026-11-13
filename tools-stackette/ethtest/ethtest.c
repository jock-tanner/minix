/* ethtest: raw ethernet through /dev/eth0: sends a broadcast frame, then reads one. */
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/ioctl.h>
#include <net/gen/ether.h>
#include <net/gen/eth_io.h>
#include <net/ioctl.h>
#include <net/gen/eth_io.h>

static void hex(v, n) unsigned v; int n;
{
  char c[4];
  int i;
  for (i = 0; i < n; i++) { c[i] = "0123456789abcdef"[(v >> (4 * (n - 1 - i))) & 15]; }
  write(1, c, n);
}

int main(argc, argv) int argc; char **argv;
{
  int fd, i, n;
  nwio_ethopt_t opt;
  nwio_ethstat_t st;
  unsigned char f[64], r[1514];

  fd = open("/dev/eth0", O_RDWR);
  if (fd < 0) { write(1, "open failed\n", 12); return(1); }
  opt.nweo_flags = NWEO_COPY | NWEO_EN_LOC | NWEO_EN_BROAD | NWEO_REMANY | NWEO_TYPEANY | NWEO_RWDATALL;
  if (ioctl(fd, NWIOSETHOPT, &opt) < 0) { write(1, "sethopt failed\n", 15); return(1); }
  if (ioctl(fd, NWIOGETHSTAT, &st) < 0) { write(1, "gethstat failed\n", 16); return(1); }
  write(1, "addr ", 5);
  for (i = 0; i < 6; i++) hex(st.nwes_addr.ea_addr[i], 2);
  write(1, "\n", 1);
  for (i = 0; i < 6; i++) f[i] = 0xff;
  memcpy(f + 6, st.nwes_addr.ea_addr, 6);
  f[12] = 0x12; f[13] = 0x34;
  for (i = 14; i < 60; i++) f[i] = i;
  n = write(fd, f, 60);
  write(1, "wrote ", 6); hex(n, 4); write(1, "\n", 1);
  if (argc > 1) {
	n = read(fd, r, sizeof(r));
	write(1, "read ", 5); hex(n, 4); write(1, ":", 1);
	for (i = 0; i < 20 && i < n; i++) { write(1, " ", 1); hex(r[i], 2); }
	write(1, "\n", 1);
  }
  return(0);
}
