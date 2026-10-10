// Host test of the expression compiler: cc -O1 -o /tmp/test_expr tools/test_expr.c -lm && /tmp/test_expr
#include "../src/ff.c"
#include "../src/expr.c"
#include <stdio.h>

static int fails;

static void ok(const char *src, float x, float want)
{
	expr_t e;
	const char *err;
	int pos;
	if (expr_compile(&e, src, &err, &pos)) {
		printf("FAIL %-24s compile error: %s at %d\n", src, err, pos);
		fails++;
		return;
	}
	float got = ff_float(expr_eval(&e, ff_of(x)));
	if (!(fabsf(got - want) <= 1e-4f * (1 + fabsf(want)))) {
		printf("FAIL %-24s x=%g: got %g, want %g\n", src, x, got, want);
		fails++;
	}
}

static void bad(const char *src)
{
	expr_t e;
	const char *err;
	int pos;
	if (!expr_compile(&e, src, &err, &pos)) {
		printf("FAIL %-24s compiled, expected an error\n", src);
		fails++;
	}
	else
		printf("ok   %-24s -> %s at %d\n", src, err, pos);
}

int main(void)
{
	ok("1+2*3", 0, 7);
	ok("2x", 3, 6);
	ok("2x^2", 3, 18);
	ok("-x^2", 3, -9);
	ok("x^-1", 4, 0.25f);
	ok("2^3^2", 0, 512);
	ok("(x+1)(x-1)", 3, 8);
	ok("3sin(x)", 1, 3 * sinf(1));
	ok("sin(x)^2+cos(x)^2", 0.7f, 1);
	ok("4cos(2x)exp(-x^2/30)", 1.5f, 4 * cosf(3) * expf(-2.25f / 30));
	ok("x^2/4-3", 2, -2);
	ok("y=x+1", 1, 2);
	ok("pi", 0, (float)M_PI);
	ok("2pi x", 1, 2 * (float)M_PI);
	ok("e^(x)", 1, (float)M_E);
	ok("sqrt(abs(x))", -9, 3);
	ok("(-2)^3", 0, -8);
	ok("log(1000)", 0, 3);
	ok("ln(e)", 0, 1);
	ok("asin(1)", 0, (float)M_PI / 2);
	ok("sinh(0)+cosh(0)", 0, 1);
	ok("1.5x", 2, 3);
	ok(".5+x", 1, 1.5f);
	ok("x*10^2", 3, 300);
	ok("floor(x)", 2.7f, 2);
	ok("floor(x)", -2.7f, -3);
	ok("x/4", 2, 0.5f);
	ok("2sqrt(2)x", 1, 2 * sqrtf(2));
	ok("tanh(x)", 0.5f, tanhf(0.5f));
	ok("sinh(x)", 1, sinhf(1));
	ok("x^0.5", 4, 2);
	ok("x^0", 7, 1);
	ok("2^x", 10, 1024);
	ok("xsin(x)", 2, 2 * sinf(2));
	bad("");
	bad("2+");
	bad("sin x");
	bad("(x+1");
	bad("x+1)");
	bad("2$x");
	printf(fails ? "%d FAILED\n" : "all passed\n", fails);
	return fails != 0;
}
