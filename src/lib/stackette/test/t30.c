#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static int cmp(const void *a, const void *b) { return *(const int *)a - *(const int *)b; }

int main(argc, argv)
int argc;
char **argv;
{
  char buf[64];
  int v[6], i;
  char *p;
  long l;

  printf("argc=%d argv0=%s\n", argc, argv[0]);
  printf("%d %u %x %o %c %s %5d|%-5d|\n", -42, 65535U, 255, 8, 'z', "str", 7, 7);
  sprintf(buf, "%ld/%s", 123456L, "ok");
  printf("%s %d\n", buf, (int) strlen(buf));
  v[0] = 5; v[1] = 3; v[2] = 9; v[3] = 1; v[4] = 7; v[5] = 2;
  qsort(v, 6, sizeof(int), cmp);
  for (i = 0; i < 6; i++) printf("%d ", v[i]);
  printf("\n");
  p = (char *) malloc(100);
  strcpy(p, "heap");
  printf("%s %d %ld %ld\n", p, atoi("-77"), strtol("0x1f", (char **) 0, 0), atol("99999"));
  free(p);
  printf("%d %d %s\n", toupper('a'), isdigit('5') != 0, strchr("hello", 'l'));
  printf("%f %e\n", 3.25, 1250.0);
  return 0;
}
