// Elementary functions on ff: argument reduction, then short polynomials. Accurate to about
// 1e-6 relative over the ranges a plot uses (far below a pixel); not correctly rounded.

#include "ff.h"

#define C(f) ff_of(f)

/* pi/2 and ln 2 in three parts whose high parts end in 12 zero bits, so that k * part is exact
   for |k| < 2^12 (Cody-Waite reduction) */
#define PIO2_1 C(1.5703125f)
#define PIO2_2 C(4.8375129699707031e-04f)
#define PIO2_3 C(7.5497901264043321e-08f)
#define LN2_1  C(0.693115234375f)
#define LN2_2  C(3.1933188438415527e-05f)
#define LN2_3  C(1.2996506981721723e-08f)

/* x = k pi/2 + r, |r| <= pi/4 */
static int32_t reduce_pio2(ff x, ff *r)
{
	int32_t k = ff_to_int_clamp(ff_mul(x, C(0.63661977236758134f)), -(1 << 20), 1 << 20);
	ff kf = ff_from_int(k);
	ff t = ff_sub(x, ff_mul(kf, PIO2_1));
	t = ff_sub(t, ff_mul(kf, PIO2_2));
	*r = ff_sub(t, ff_mul(kf, PIO2_3));
	return k;
}

/* Taylor on |r| <= pi/4: the first omitted terms are below 2e-9 */
static ff sin_r(ff r)
{
	ff r2 = ff_mul(r, r);
	ff p = C(2.7557319e-6f);
	p = ff_add(ff_mul(p, r2), C(-1.9841270e-4f));
	p = ff_add(ff_mul(p, r2), C(8.3333333e-3f));
	p = ff_add(ff_mul(p, r2), C(-1.6666667e-1f));
	return ff_add(r, ff_mul(ff_mul(r, r2), p));
}

static ff cos_r(ff r)
{
	ff r2 = ff_mul(r, r);
	ff p = C(-2.7557319e-7f);
	p = ff_add(ff_mul(p, r2), C(2.4801587e-5f));
	p = ff_add(ff_mul(p, r2), C(-1.3888889e-3f));
	p = ff_add(ff_mul(p, r2), C(4.1666667e-2f));
	p = ff_add(ff_mul(p, r2), C(-0.5f));
	return ff_add(FF_ONE, ff_mul(r2, p));
}

static int too_big(ff x)
{
	/* |x| >= 2^20 or not finite: no meaningful reduction left in single precision */
	return ((x >> 23) & 0xff) >= 127 + 20;
}

ff ff_sin(ff x)
{
	if (too_big(x))
		return FF_NAN;
	ff r;
	int32_t k = reduce_pio2(x, &r);
	switch (k & 3) {
	case 0: return sin_r(r);
	case 1: return cos_r(r);
	case 2: return ff_neg(sin_r(r));
	default: return ff_neg(cos_r(r));
	}
}

ff ff_cos(ff x)
{
	if (too_big(x))
		return FF_NAN;
	ff r;
	int32_t k = reduce_pio2(x, &r);
	switch (k & 3) {
	case 0: return cos_r(r);
	case 1: return ff_neg(sin_r(r));
	case 2: return ff_neg(cos_r(r));
	default: return sin_r(r);
	}
}

ff ff_tan(ff x)
{
	if (too_big(x))
		return FF_NAN;
	ff r;
	int32_t k = reduce_pio2(x, &r);
	ff s = sin_r(r), c = cos_r(r);
	return (k & 1) ? ff_neg(ff_div(c, s)) : ff_div(s, c);
}

ff ff_exp(ff x)
{
	if (!ff_finite(x))
		return (x & 0x7fffff) ? FF_NAN : (x & FF_SIGN) ? 0 : FF_INF;
	if (ff_lt(C(88.72f), x))
		return FF_INF;
	if (ff_lt(x, C(-87.33f)))
		return 0;
	int32_t k = ff_to_int_clamp(ff_mul(x, C(1.4426950408889634f)), -200, 200);
	ff kf = ff_from_int(k);
	ff r = ff_sub(x, ff_mul(kf, LN2_1));
	r = ff_sub(r, ff_mul(kf, LN2_2));
	r = ff_sub(r, ff_mul(kf, LN2_3));
	/* |r| <= ln2/2: degree 7, error below 1e-8 */
	ff p = C(1.9841270e-4f);
	p = ff_add(ff_mul(p, r), C(1.3888889e-3f));
	p = ff_add(ff_mul(p, r), C(8.3333333e-3f));
	p = ff_add(ff_mul(p, r), C(4.1666667e-2f));
	p = ff_add(ff_mul(p, r), C(1.6666667e-1f));
	p = ff_add(ff_mul(p, r), C(0.5f));
	p = ff_add(ff_mul(p, r), FF_ONE);
	p = ff_add(ff_mul(p, r), FF_ONE);
	/* times 2^k: add k to the exponent */
	int e = (int)((p >> 23) & 0xff) + k;
	if (e >= 0xff)
		return FF_INF;
	if (e <= 0)
		return 0;
	return (p & ~(0xffu << 23)) | (uint32_t)e << 23;
}

ff ff_log(ff x)
{
	if (x & FF_SIGN)
		return (x & ~FF_SIGN) ? FF_NAN : FF_SIGN | FF_INF;
	if (!ff_finite(x))
		return x;
	int e = (x >> 23) & 0xff;
	if (!e)
		return FF_SIGN | FF_INF;
	/* x = 2^k m, m in [sqrt(1/2), sqrt(2)) */
	int k = e - 127;
	ff m = (x & 0x7fffff) | (127u << 23);
	if (ff_lt(C(1.41421356f), m)) {
		m = (x & 0x7fffff) | (126u << 23);
		k++;
	}
	/* log m = 2 atanh(s), s = (m-1)/(m+1), |s| <= 0.172 */
	ff s = ff_div(ff_sub(m, FF_ONE), ff_add(m, FF_ONE));
	ff s2 = ff_mul(s, s);
	ff p = C(1.0f / 9);
	p = ff_add(ff_mul(p, s2), C(1.0f / 7));
	p = ff_add(ff_mul(p, s2), C(1.0f / 5));
	p = ff_add(ff_mul(p, s2), C(1.0f / 3));
	p = ff_add(ff_mul(p, s2), FF_ONE);
	ff lm = ff_mul(ff_add(s, s), p);
	ff kf = ff_from_int(k);
	return ff_add(ff_add(ff_mul(kf, LN2_1), ff_add(ff_mul(kf, LN2_2), ff_mul(kf, LN2_3))), lm);
}

ff ff_sqrt(ff x)
{
	if (x & FF_SIGN)
		return (x & ~FF_SIGN) ? FF_NAN : x;
	if (!x || !ff_finite(x))
		return x;
	/* reciprocal square root: the classic first guess, three Newton steps, then times x */
	ff y = 0x5f3759dfu - (x >> 1);
	ff hx = ff_mul(x, C(0.5f));
	for (int i = 0; i < 3; i++)
		y = ff_mul(y, ff_sub(C(1.5f), ff_mul(hx, ff_mul(y, y))));
	return ff_mul(x, y);
}

ff ff_atan(ff x)
{
	uint32_t s = x & FF_SIGN;
	ff t = x & ~FF_SIGN;
	if (!ff_finite(t))
		return (t & 0x7fffff) ? FF_NAN : s | C(1.5707963f);
	ff base = 0;
	int inv = 0;
	if (ff_lt(FF_ONE, t)) {
		t = ff_div(FF_ONE, t);
		inv = 1;
	}
	/* above tan(pi/12): atan t = pi/6 + atan((t sqrt3 - 1) / (t + sqrt3)) */
	if (ff_lt(C(0.26794919f), t)) {
		t = ff_div(ff_sub(ff_mul(t, C(1.7320508f)), FF_ONE), ff_add(t, C(1.7320508f)));
		base = C(0.52359878f);
	}
	ff t2 = ff_mul(t, t);
	ff p = C(1.0f / 9);
	p = ff_add(ff_mul(p, t2), C(-1.0f / 7));
	p = ff_add(ff_mul(p, t2), C(1.0f / 5));
	p = ff_add(ff_mul(p, t2), C(-1.0f / 3));
	ff a = ff_add(base, ff_add(t, ff_mul(ff_mul(t, t2), p)));
	if (inv)
		a = ff_sub(C(1.5707963f), a);
	return s | a;
}

ff ff_asin(ff x)
{
	ff ax = x & ~FF_SIGN;
	if (ff_lt(FF_ONE, ax))
		return FF_NAN;
	if (ax == FF_ONE)
		return (x & FF_SIGN) | C(1.5707963f);
	return ff_atan(ff_div(x, ff_sqrt(ff_sub(FF_ONE, ff_mul(x, x)))));
}

ff ff_acos(ff x)
{
	ff a = ff_asin(x);
	return ff_finite(a) ? ff_sub(C(1.5707963f), a) : a;
}

ff ff_pow(ff a, ff b)
{
	if (!(a & ~FF_SIGN))
		return (b & FF_SIGN) ? FF_INF : (b & ~FF_SIGN) ? 0 : FF_ONE;
	if (a & FF_SIGN)
		return FF_NAN;  /* integer exponents of negative bases are done by the caller */
	return ff_exp(ff_mul(b, ff_log(a)));
}

ff ff_floor(ff x)
{
	if (((x >> 23) & 0xff) >= 127 + 23)
		return x;  /* already an integer (or not finite) */
	int32_t i = ff_trunc_int(x);
	ff t = ff_from_int(i);
	return (x & FF_SIGN) && t != x && (x & ~FF_SIGN) ? ff_from_int(i - 1) : t;
}

ff ff_ceil(ff x)
{
	return ff_neg(ff_floor(ff_neg(x)));
}
