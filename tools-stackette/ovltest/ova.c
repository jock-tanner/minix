extern int b(int x);
extern int c(int x);
extern int odd(int n);

static int twice(int x)
{
	return x * 2;
}

int a(int x)
{
	return b(x + twice(0)) + 10;
}

int even(int n)
{
	if (n == 0) return 1;
	return odd(n - 1);
}
