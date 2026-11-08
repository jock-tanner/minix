/* the processes of hw23 */

int kputc(int c);
int kyield(void);
void kexit(void);

long fact(long n)
{
  return n <= 1 ? 1L : n * fact(n - 1);
}

int fib(int n)
{
  return n < 2 ? n : fib(n - 1) + fib(n - 2);
}

struct rec {
  int a;
  long b;
  char c[5];
};

char msg[] = "DEF";
char buf[8];
struct rec r1 = {7, 70000L, "xyz"}, r2;

int sw(int n)
{
  switch (n) {
  case 1: return 10;
  case 2: return 20;
  case 3: return 30;
  case 7: return 70;
  default: return 0;
  }
}

int main(int argc, char **argv, char **envp)
{
  int i, ok;
  long l;

  kputc('B');
  kyield();
  /* the second turn: arithmetic */
  ok = fib(10) == 55 && fact(7L) == 5040L;
  l = 100000L;
  ok = ok && l / 7 == 14285L && l % 7 == 5 && (l << 3) == 800000L;
  kputc(ok ? 'D' : 'x');
  kyield();
  /* the third turn: argc, argv, envp, the structure */
  r2 = r1;
  ok = argc == 1 && argv[0][0] == 'p' && argv[1] == 0 && envp[0] == 0;
  ok = ok && r2.a == 7 && r2.b == 70000L && r2.c[2] == 'z';
  kputc(ok ? 'F' : 'x');
  kyield();
  /* the fourth turn: the switch */
  ok = sw(1) == 10 && sw(3) == 30 && sw(7) == 70 && sw(5) == 0;
  kputc(ok ? 'H' : 'x');
  kexit();
  return 0;
}
