int kputc(int c);
int kyield(void);
void kexit(void);

char msg[] = "DEF";
char buf[8];
int counter = 40;

int main(int argc, char **argv, char **envp)
{
  int i, ok;
  long a, b;
  unsigned u;

  kputc('C');
  kyield();
  /* strings: the data, the bss, a loop */
  for (i = 0; msg[i]; i++) buf[i] = msg[i] + 1;
  buf[i] = 0;
  ok = buf[0] == 'E' && buf[1] == 'F' && buf[2] == 'G' && counter == 40;
  kputc(ok ? 'E' : 'x');
  kyield();
  /* 32-bit arithmetic */
  a = 123456L;
  b = -789L;
  u = 0xfedc;
  ok = a * b == -97406784L && a / b == -156L && a % b == 372L;
  ok = ok && (u >> 4) == 0xfed && (u & 0xff) == 0xdc && ((long) u << 8) == 0xfedc00L;
  kputc(ok ? 'G' : 'x');
  kyield();
  /* the peripherals are not there for a process: the fault */
  *(volatile int *) 0xf000 = 'Q';
  kputc('x');
  kexit();
  return 0;
}
