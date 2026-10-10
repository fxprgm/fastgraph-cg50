// Fast single-precision floats for a CPU without an FPU.
//
// Values are IEEE-754 single-precision bit patterns (ff), but the arithmetic is ours, written with
// integer instructions: no rounding modes, no exceptions, denormals flushed to zero, NaN and
// infinity handled only as far as a plotter needs (they stay non-finite). Round-to-nearest on
// add, sub and mul. Several times faster than libgcc's general soft-float routines.
#pragma once
#include <stdint.h>

typedef uint32_t ff;

#define FF_SIGN 0x80000000u
#define FF_INF  0x7f800000u
#define FF_NAN  0x7fc00000u
#define FF_ZERO 0u
#define FF_ONE  0x3f800000u

static inline ff ff_of(float f)
{
	union { float f; uint32_t u; } c = {f};
	return c.u;
}

static inline float ff_float(ff a)
{
	union { uint32_t u; float f; } c = {a};
	return c.f;
}

static inline int ff_finite(ff a)
{
	return (a & FF_INF) != FF_INF;
}

static inline ff ff_neg(ff a)
{
	return a ^ FF_SIGN;
}

static inline ff ff_abs(ff a)
{
	return a & ~FF_SIGN;
}

/* Signed-comparable key: orders floats like the integers they map to (NaN aside) */
static inline int32_t ff_key(ff a)
{
	return (int32_t)a < 0 ? (int32_t)(0x80000000u - a) : (int32_t)a;
}

static inline int ff_lt(ff a, ff b)
{
	return ff_key(a) < ff_key(b);
}

static inline ff ff_mul(ff a, ff b)
{
	uint32_t s = (a ^ b) & FF_SIGN;
	int ea = (a >> 23) & 0xff, eb = (b >> 23) & 0xff;
	if (ea == 0xff || eb == 0xff) {
		/* NaN in, or infinity times zero: NaN */
		if ((ea == 0xff && (a & 0x7fffff)) || (eb == 0xff && (b & 0x7fffff)) || !ea || !eb)
			return FF_NAN;
		return s | FF_INF;
	}
	if (!ea || !eb)
		return s;
	uint64_t p = (uint64_t)((a & 0x7fffff) | 0x800000) * ((b & 0x7fffff) | 0x800000);
	int e = ea + eb - 127;
	uint32_t m;
	/* p is in [2^46, 2^48): keep 24 bits plus one for rounding */
	if (p & (1ull << 47)) {
		m = (uint32_t)(p >> 23);
		e++;
	}
	else
		m = (uint32_t)(p >> 22);
	m = (m + 1) >> 1;
	if (m & 0x1000000) {
		m >>= 1;
		e++;
	}
	if (e >= 0xff)
		return s | FF_INF;
	if (e <= 0)
		return s;
	return s | (uint32_t)e << 23 | (m & 0x7fffff);
}

static inline ff ff_add(ff a, ff b)
{
	if ((a & ~FF_SIGN) < (b & ~FF_SIGN)) {
		ff t = a;
		a = b;
		b = t;
	}
	int ea = (a >> 23) & 0xff, eb = (b >> 23) & 0xff;
	if (ea == 0xff) {
		if (eb == 0xff && ((a ^ b) & FF_SIGN))
			return FF_NAN;
		return a;
	}
	if (!eb)
		return ea ? a : (a & b & FF_SIGN);
	int d = ea - eb;
	if (d > 26)
		return a;
	/* 24-bit mantissas with 6 extra low bits */
	uint32_t ma = ((a & 0x7fffff) | 0x800000) << 6;
	uint32_t mb = (((b & 0x7fffff) | 0x800000) << 6) >> d;
	uint32_t s = a & FF_SIGN;
	int e = ea;
	if ((a ^ b) & FF_SIGN) {
		ma -= mb;
		if (!ma)
			return 0;
		/* normalise so that bit 29 is the leading one */
		if (ma < (1u << 14)) { ma <<= 16; e -= 16; }
		if (ma < (1u << 22)) { ma <<= 8; e -= 8; }
		if (ma < (1u << 26)) { ma <<= 4; e -= 4; }
		if (ma < (1u << 28)) { ma <<= 2; e -= 2; }
		if (ma < (1u << 29)) { ma <<= 1; e -= 1; }
	}
	else {
		ma += mb;
		if (ma & (1u << 30)) {
			ma = (ma >> 1) | (ma & 1);
			e++;
		}
	}
	uint32_t m = (ma + 32) >> 6;
	if (m & 0x1000000) {
		m >>= 1;
		e++;
	}
	if (e >= 0xff)
		return s | FF_INF;
	if (e <= 0)
		return s;
	return s | (uint32_t)e << 23 | (m & 0x7fffff);
}

static inline ff ff_sub(ff a, ff b)
{
	return ff_add(a, b ^ FF_SIGN);
}

static inline ff ff_div(ff a, ff b)
{
	uint32_t s = (a ^ b) & FF_SIGN;
	int ea = (a >> 23) & 0xff, eb = (b >> 23) & 0xff;
	if (ea == 0xff || eb == 0xff) {
		if ((ea == 0xff && (a & 0x7fffff)) || (eb == 0xff && (b & 0x7fffff)) || ea == eb)
			return FF_NAN;
		return ea == 0xff ? s | FF_INF : s;
	}
	if (!eb)
		return ea ? s | FF_INF : FF_NAN;
	if (!ea)
		return s;
	uint32_t ma = (a & 0x7fffff) | 0x800000, mb = (b & 0x7fffff) | 0x800000;
	int e = ea - eb + 127;
	if (ma < mb) {
		ma <<= 1;
		e--;
	}
	/* ma/mb in [1, 2): 25 quotient bits, the last one for rounding */
	uint32_t q = (uint32_t)(((uint64_t)ma << 24) / mb);
	q = (q + 1) >> 1;
	if (q & 0x1000000) {
		q >>= 1;
		e++;
	}
	if (e >= 0xff)
		return s | FF_INF;
	if (e <= 0)
		return s;
	return s | (uint32_t)e << 23 | (q & 0x7fffff);
}

static inline ff ff_from_int(int32_t i)
{
	if (!i)
		return 0;
	uint32_t s = 0, m = i;
	if (i < 0) {
		s = FF_SIGN;
		m = -(uint32_t)i;
	}
	int e = 127 + 31;
	if (m < (1u << 16)) { m <<= 16; e -= 16; }
	if (m < (1u << 24)) { m <<= 8; e -= 8; }
	if (m < (1u << 28)) { m <<= 4; e -= 4; }
	if (m < (1u << 30)) { m <<= 2; e -= 2; }
	if (m < (1u << 31)) { m <<= 1; e -= 1; }
	/* m has its leading one at bit 31: round to 24 bits */
	uint32_t r = (m >> 7) + 1;
	r >>= 1;
	if (r & 0x1000000) {
		r >>= 1;
		e++;
	}
	return s | (uint32_t)e << 23 | (r & 0x7fffff);
}

/* Rounds to the nearest integer, clamped to [lo, hi]; NaN gives lo */
static inline int32_t ff_to_int_clamp(ff a, int32_t lo, int32_t hi)
{
	int e = (a >> 23) & 0xff;
	if (e == 0xff && (a & 0x7fffff))
		return lo;
	if (e < 126)
		return 0;
	if (e > 127 + 29)
		return (a & FF_SIGN) ? lo : hi;
	uint32_t m = (a & 0x7fffff) | 0x800000;
	int sh = 150 - e;
	int32_t v;
	if (sh > 0)
		v = (int32_t)((m + (1u << (sh - 1))) >> sh);
	else
		v = (int32_t)(m << -sh);
	if (a & FF_SIGN)
		v = -v;
	return v < lo ? lo : v > hi ? hi : v;
}

/* Truncating conversion for |a| < 2^30 (floor/ceil build on it) */
static inline int32_t ff_trunc_int(ff a)
{
	int e = (a >> 23) & 0xff;
	if (e < 127)
		return 0;
	uint32_t m = (a & 0x7fffff) | 0x800000;
	int sh = 150 - e;
	int32_t v = sh >= 0 ? (int32_t)(m >> sh) : (int32_t)(m << -sh);
	return (a & FF_SIGN) ? -v : v;
}

ff ff_sin(ff x);
ff ff_cos(ff x);
ff ff_tan(ff x);
ff ff_exp(ff x);
ff ff_log(ff x);
ff ff_sqrt(ff x);
ff ff_atan(ff x);
ff ff_asin(ff x);
ff ff_acos(ff x);
ff ff_pow(ff a, ff b);
ff ff_floor(ff x);
ff ff_ceil(ff x);
