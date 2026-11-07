/* A stand-in for the kernel, for the model: the calls of the library go here. */
#include <lib.h>
#include <minix/callnr.h>
#include <minix/com.h>
#include <string.h>

#define UART ((volatile unsigned char *) 0xf000)

int _sendrec(who, m)
int who;
message *m;
{
  char *p;
  int n, i;

  switch (m->m_type) {
  case WRITE:
	p = m->m1_p1;
	n = m->m1_i2;
	for (i = 0; i < n; i++) *UART = p[i];
	m->m_type = n;
	break;
  case EXIT:
	for (;;);
  case BRK:
	m->m2_p1 = m->m1_p1;
	m->m_type = 0;
	break;
  default:
	m->m_type = -1;
  }
  return 0;
}
