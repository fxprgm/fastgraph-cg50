// Host test of the ff arithmetic and functions against the host's IEEE floats and libm.
//   cc -O1 -o /tmp/test_ff tools/test_ff.c -lm && /tmp/test_ff
#include "../src/ff.c"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static uint32_t rng = 12345;
static uint32_t rnd(void)
{
	rng ^= rng << 13;
	rng ^= rng >> 17;
	rng ^= rng << 5;
	return rng;
}

/* a random float with an exponent in [lo, hi] (powers of two) and a random sign */
static float rf(int lo, int hi)
{
	int e = lo + (int)(rnd() % (uint32_t)(hi - lo + 1));
	uint32_t b = (rnd() & 0x807fffff) | (uint32_t)(e + 127) << 23;
	return ff_float(b);
}

static int ulps(float a, float b)
{
	if (isnan(a) || isnan(b))
		return isnan(a) && isnan(b) ? 0 : 1000000;
	int32_t ka = ff_key(ff_of(a)), kb = ff_key(ff_of(b));
	return abs(ka - kb);
}

static int fails;

static void arith(const char *name, ff (*f)(ff, ff), float (*ref)(float, float))
{
	int worst = 0;
	for (int i = 0; i < 200000; i++) {
		float a = rf(-30, 30), b = rf(-30, 30);
		if (i % 7 == 0)
			b = ff_float(ff_of(a) + (rnd() % 5) - 2);  /* near-equal: cancellation */
		int u = ulps(ff_float(f(ff_of(a), ff_of(b))), ref(a, b));
		if (u > worst)
			worst = u;
		if (u > 1 && fails < 20) {
			printf("  %s(%.9g, %.9g) = %.9g, want %.9g\n", name, a, b,
				ff_float(f(ff_of(a), ff_of(b))), ref(a, b));
			fails++;
		}
	}
	printf("%-5s worst %d ulp\n", name, worst);
}

static float radd(float a, float b) { return a + b; }
static float rsub(float a, float b) { return a - b; }
static float rmul(float a, float b) { return a * b; }
static float rdiv(float a, float b) { return a / b; }
static ff fadd(ff a, ff b) { return ff_add(a, b); }
static ff fsub(ff a, ff b) { return ff_sub(a, b); }
static ff fmul(ff a, ff b) { return ff_mul(a, b); }
static ff fdiv(ff a, ff b) { return ff_div(a, b); }

static void func(const char *name, ff (*f)(ff), double (*ref)(double), float lo, float hi, double tol)
{
	double worst = 0;
	float wx = 0;
	for (int i = 0; i <= 100000; i++) {
		float x = lo + (hi - lo) * i / 100000.0f;
		double want = ref(x), got = ff_float(f(ff_of(x)));
		if (isnan(want) && isnan(got))
			continue;
		if (isinf(want) && got == want)
			continue;
		double err = fabs(got - want) / (fabs(want) > 1 ? fabs(want) : 1);
		if (!(err <= worst))
			worst = err, wx = x;
	}
	printf("%-6s [%g, %g] worst error %.2g at %.7g %s\n", name, lo, hi, worst, wx, worst <= tol ? "" : "FAIL");
	if (!(worst <= tol))
		fails++;
}

int main(void)
{
	arith("add", fadd, radd);
	arith("sub", fsub, rsub);
	arith("mul", fmul, rmul);
	arith("div", fdiv, rdiv);

	int bad = 0;
	for (int i = 0; i < 100000; i++) {
		int32_t v = (int32_t)rnd() >> (rnd() % 31);
		if (ulps(ff_float(ff_from_int(v)), (float)v) > 1)  /* ties above 2^24 round up, not to even */
			bad++;
		float x = rf(-3, 12);
		int32_t r = ff_to_int_clamp(ff_of(x), -100000, 100000);
		long want = lroundf(x);
		if (want < -100000) want = -100000;
		if (want > 100000) want = 100000;
		if (r != want && fabsf(x - truncf(x)) != 0.5f)
			bad++;
	}
	printf("int conversions: %d mismatches\n", bad);
	fails += bad > 0;

	/* special values */
	if (ff_finite(ff_div(FF_ONE, 0)) || ff_finite(ff_log(0)) || ff_finite(ff_sqrt(ff_of(-1))))
		printf("special values FAIL\n"), fails++;

	func("sin", ff_sin, sin, -100, 100, 2e-6);
	func("cos", ff_cos, cos, -100, 100, 2e-6);
	func("tan", ff_tan, tan, -1.5f, 1.5f, 2e-6);
	func("exp", ff_exp, exp, -80, 80, 2e-6);
	func("log", ff_log, log, 1e-6f, 1e6f, 2e-6);
	func("log", ff_log, log, 0.01f, 3, 2e-6);
	func("sqrt", ff_sqrt, sqrt, 0, 1e6f, 2e-6);
	func("sqrt", ff_sqrt, sqrt, 0, 2, 2e-6);
	func("atan", ff_atan, atan, -50, 50, 2e-6);
	func("asin", ff_asin, asin, -1, 1, 2e-5);
	func("acos", ff_acos, acos, -1, 1, 2e-5);
	func("floor", ff_floor, floor, -1000, 1000, 0);
	func("ceil", ff_ceil, ceil, -1000, 1000, 0);
	printf(fails ? "%d FAILED\n" : "all passed\n", fails);
	return fails != 0;
}
