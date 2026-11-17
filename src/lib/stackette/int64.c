/* 64 bit disk addresses (include/minix/u64.h) in C: the Intel versions are
 * assembly. u64_t is two u32_t, the low one first.
 */
#include <sys/types.h>
#include <minix/u64.h>

#define LO(x)	((x)._[0])
#define HI(x)	((x)._[1])

u64_t add64(i, j)
u64_t i, j;
{
  u64_t r;

  LO(r) = LO(i) + LO(j);
  HI(r) = HI(i) + HI(j) + (LO(r) < LO(i));
  return(r);
}

u64_t add64ul(i, j)
u64_t i;
unsigned long j;
{
  u64_t r;

  LO(r) = LO(i) + j;
  HI(r) = HI(i) + (LO(r) < LO(i));
  return(r);
}

u64_t add64u(i, j)
u64_t i;
unsigned j;
{
  return(add64ul(i, (unsigned long) j));
}

u64_t sub64(i, j)
u64_t i, j;
{
  u64_t r;

  LO(r) = LO(i) - LO(j);
  HI(r) = HI(i) - HI(j) - (LO(i) < LO(j));
  return(r);
}

u64_t sub64ul(i, j)
u64_t i;
unsigned long j;
{
  u64_t r;

  LO(r) = LO(i) - j;
  HI(r) = HI(i) - (LO(i) < j);
  return(r);
}

u64_t sub64u(i, j)
u64_t i;
unsigned j;
{
  return(sub64ul(i, (unsigned long) j));
}

unsigned diff64(i, j)
u64_t i, j;
{
  return((unsigned) (LO(i) - LO(j)));
}

u64_t cvul64(i)
unsigned long i;
{
  u64_t r;

  LO(r) = i;
  HI(r) = 0;
  return(r);
}

u64_t cvu64(i)
unsigned i;
{
  return(cvul64((unsigned long) i));
}

unsigned long cv64ul(i)
u64_t i;
{
  return(LO(i));
}

unsigned cv64u(i)
u64_t i;
{
  return((unsigned) LO(i));
}

unsigned long ex64lo(i)
u64_t i;
{
  return(LO(i));
}

unsigned long ex64hi(i)
u64_t i;
{
  return(HI(i));
}

u64_t make64(lo, hi)
unsigned long lo, hi;
{
  u64_t r;

  LO(r) = lo;
  HI(r) = hi;
  return(r);
}

unsigned long div64u(i, j)
u64_t i;
unsigned j;
{
/* The quotient of 64 by 16 bits; the low 32 bits of it. A power of two is a
 * shift, a short dividend a long division, the rest is done bit by bit.
 */
  unsigned long q = 0, rem = 0, hi = HI(i), lo = LO(i);
  int b;

  if ((j & (j - 1)) == 0 && j != 0) {
	for (b = 0; (1 << b) != j; b++) {}
	if (b == 0) return(lo);
	return((lo >> b) | (hi << (32 - b)));
  }
  if (hi == 0) return(lo / j);

  for (b = 63; b >= 0; b--) {
	rem = (rem << 1) | (b >= 32 ? (hi >> (b - 32)) & 1 : (lo >> b) & 1);
	q <<= 1;
	if (rem >= j) {
		rem -= j;
		q |= 1;
	}
  }
  return(q);
}

unsigned rem64u(i, j)
u64_t i;
unsigned j;
{
  unsigned long rem = 0, hi = HI(i), lo = LO(i);
  int b;

  for (b = 63; b >= 0; b--) {
	rem = (rem << 1) | (b >= 32 ? (hi >> (b - 32)) & 1 : (lo >> b) & 1);
	if (rem >= j) rem -= j;
  }
  return((unsigned) rem);
}

u64_t mul64u(i, j)
unsigned long i;
unsigned j;
{
/* 32 by 16 bits, in two halves */
  u64_t r;
  unsigned long a = (i & 0xffff) * (unsigned long) j;
  unsigned long b = (i >> 16) * (unsigned long) j;

  LO(r) = a + (b << 16);
  HI(r) = (b >> 16) + (LO(r) < a);
  return(r);
}

int cmp64(i, j)
u64_t i, j;
{
  if (HI(i) != HI(j)) return(HI(i) < HI(j) ? -1 : 1);
  if (LO(i) != LO(j)) return(LO(i) < LO(j) ? -1 : 1);
  return(0);
}

int cmp64ul(i, j)
u64_t i;
unsigned long j;
{
  if (HI(i) != 0) return(1);
  if (LO(i) != j) return(LO(i) < j ? -1 : 1);
  return(0);
}

int cmp64u(i, j)
u64_t i;
unsigned j;
{
  return(cmp64ul(i, (unsigned long) j));
}
