// Recursive-descent parser emitting postfix code, and its evaluator.
//
//   expr    := term (('+' | '-') term)*
//   term    := unary (('*' | '/') unary | unary)*      -- the bare case is 2x, 3sin(x), (x+1)(x-1)
//   unary   := ('-' | '+') unary | power
//   power   := primary ('^' unary)?                    -- right-associative, 2^-x allowed
//   primary := number | x | pi | e | name '(' expr ')' | '(' expr ')'
//
// So -x^2 = -(x^2) and 2x^2 = 2(x^2), as on a calculator. Numbers are single-precision floats in
// our own fast arithmetic (ff.h): the CPU has no FPU. Constant sub-expressions are folded while
// compiling (2pi is one constant) and a division by a constant becomes a multiplication.

#include "expr.h"
#include <math.h>
#include <string.h>

#define PI_F ff_of(3.14159265f)
#define E_F ff_of(2.71828183f)

enum {
	OP_CONST, OP_X, OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_POW, OP_NEG, OP_SQR, OP_FN,
};

enum {
	F_SIN, F_COS, F_TAN, F_ASIN, F_ACOS, F_ATAN, F_SINH, F_COSH, F_TANH,
	F_SQRT, F_ABS, F_LN, F_LOG, F_EXP, F_FLOOR, F_CEIL,
};

// Longest names first, so "sinh" is not read as "sin" followed by "h"
static const struct { const char *name; int fn; } functions[] = {
	{"asin", F_ASIN}, {"acos", F_ACOS}, {"atan", F_ATAN},
	{"sinh", F_SINH}, {"cosh", F_COSH}, {"tanh", F_TANH},
	{"sqrt", F_SQRT}, {"floor", F_FLOOR}, {"ceil", F_CEIL},
	{"sin", F_SIN}, {"cos", F_COS}, {"tan", F_TAN},
	{"abs", F_ABS}, {"exp", F_EXP}, {"log", F_LOG}, {"ln", F_LN},
};

typedef struct {
	expr_t *e;
	const char *src;
	const char *p;
	const char *err;
	const char *errp;
	int depth;
	int maxdepth;
} parser_t;

static void fail(parser_t *ps, const char *msg)
{
	if (!ps->err) {
		ps->err = msg;
		ps->errp = ps->p;
	}
}

static void skip(parser_t *ps)
{
	while (*ps->p == ' ')
		ps->p++;
}

static int is_binary(int op)
{
	return op == OP_ADD || op == OP_SUB || op == OP_MUL || op == OP_DIV || op == OP_POW;
}

/* After an operator: if its operands are constants, replace the lot by the result; a division
   by a constant becomes a multiplication by its reciprocal. */
static void fold(expr_t *e)
{
	expr_insn_t *c = e->code;
	int n = e->n, op = c[n - 1].op;
	int arity = is_binary(op) ? 2 : (op == OP_NEG || op == OP_SQR || op == OP_FN) ? 1 : 0;
	if (!arity || n < arity + 1)
		return;
	int consts = c[n - 2].op == OP_CONST && (arity == 1 || c[n - 3].op == OP_CONST);
	if (consts) {
		expr_t tmp;
		memcpy(tmp.code, c + n - 1 - arity, (arity + 1) * sizeof *c);
		tmp.n = arity + 1;
		ff v = expr_eval(&tmp, 0);
		e->n -= arity;
		c[e->n - 1] = (expr_insn_t){OP_CONST, 0, v};
		return;
	}
	if (op == OP_DIV && c[n - 2].op == OP_CONST) {
		c[n - 2].val = ff_div(FF_ONE, c[n - 2].val);
		c[n - 1].op = OP_MUL;
	}
}

static void emit(parser_t *ps, int op, int fn, ff val)
{
	if (ps->e->n >= EXPR_MAX_CODE) {
		fail(ps, "expression too long");
		return;
	}
	ps->e->code[ps->e->n++] = (expr_insn_t){op, fn, val};
	if (op == OP_CONST || op == OP_X)
		ps->depth++;
	else if (is_binary(op))
		ps->depth--;
	if (ps->depth > ps->maxdepth)
		ps->maxdepth = ps->depth;
	fold(ps->e);
}

// Whether the next token can start a primary (for implicit multiplication)
static int starts_primary(parser_t *ps)
{
	char c = *ps->p;
	return (c >= '0' && c <= '9') || c == '.' || c == '(' || (c >= 'a' && c <= 'z');
}

static void parse_expr(parser_t *ps);
static void parse_unary(parser_t *ps);

static void parse_primary(parser_t *ps)
{
	skip(ps);
	const char *p = ps->p;
	if ((*p >= '0' && *p <= '9') || *p == '.') {
		float v = 0, scale = 1;
		int digits = 0;
		while (*p >= '0' && *p <= '9')
			v = v * 10 + (*p++ - '0'), digits++;
		if (*p == '.') {
			p++;
			while (*p >= '0' && *p <= '9')
				scale /= 10, v += (*p++ - '0') * scale, digits++;
		}
		if (!digits) {
			fail(ps, "number expected");
			return;
		}
		ps->p = p;
		emit(ps, OP_CONST, 0, ff_of(v));
		return;
	}
	if (*p == '(') {
		ps->p++;
		parse_expr(ps);
		skip(ps);
		if (*ps->p != ')') {
			fail(ps, "')' expected");
			return;
		}
		ps->p++;
		return;
	}
	for (unsigned i = 0; i < sizeof functions / sizeof *functions; i++) {
		size_t n = strlen(functions[i].name);
		if (strncmp(p, functions[i].name, n))
			continue;
		ps->p = p + n;
		skip(ps);
		if (*ps->p != '(') {
			fail(ps, "'(' expected after the function name");
			return;
		}
		ps->p++;
		parse_expr(ps);
		skip(ps);
		if (*ps->p != ')') {
			fail(ps, "')' expected");
			return;
		}
		ps->p++;
		emit(ps, OP_FN, functions[i].fn, 0);
		return;
	}
	if (!strncmp(p, "pi", 2)) {
		ps->p = p + 2;
		emit(ps, OP_CONST, 0, PI_F);
		return;
	}
	if (*p == 'x') {
		ps->p = p + 1;
		emit(ps, OP_X, 0, 0);
		return;
	}
	if (*p == 'e') {
		ps->p = p + 1;
		emit(ps, OP_CONST, 0, E_F);
		return;
	}
	fail(ps, *p ? "unexpected character" : "unexpected end");
}

static void parse_power(parser_t *ps)
{
	parse_primary(ps);
	skip(ps);
	if (*ps->p == '^') {
		ps->p++;
		skip(ps);
		// x^2 is so common that it gets its own instruction
		if (ps->p[0] == '2' && !(ps->p[1] >= '0' && ps->p[1] <= '9') && ps->p[1] != '.') {
			ps->p++;
			emit(ps, OP_SQR, 0, 0);
			return;
		}
		parse_unary(ps);
		emit(ps, OP_POW, 0, 0);
	}
}

static void parse_unary(parser_t *ps)
{
	skip(ps);
	if (*ps->p == '-') {
		ps->p++;
		parse_unary(ps);
		emit(ps, OP_NEG, 0, 0);
	}
	else if (*ps->p == '+') {
		ps->p++;
		parse_unary(ps);
	}
	else
		parse_power(ps);
}

static void parse_term(parser_t *ps)
{
	parse_unary(ps);
	for (;;) {
		skip(ps);
		char c = *ps->p;
		if (c == '*' || c == '/') {
			ps->p++;
			parse_unary(ps);
			emit(ps, c == '*' ? OP_MUL : OP_DIV, 0, 0);
		}
		else if (starts_primary(ps)) {
			parse_power(ps);
			emit(ps, OP_MUL, 0, 0);
		}
		else
			return;
		if (ps->err)
			return;
	}
}

static void parse_expr(parser_t *ps)
{
	parse_term(ps);
	for (;;) {
		skip(ps);
		char c = *ps->p;
		if (c != '+' && c != '-')
			return;
		ps->p++;
		parse_term(ps);
		emit(ps, c == '+' ? OP_ADD : OP_SUB, 0, 0);
		if (ps->err)
			return;
	}
}

int expr_compile(expr_t *e, const char *src, const char **err, int *errpos)
{
	parser_t ps = {.e = e, .src = src, .p = src};
	e->n = 0;
	skip(&ps);
	// A leading "y=" is accepted and ignored
	if (ps.p[0] == 'y' && ps.p[1] == '=')
		ps.p += 2;
	skip(&ps);
	if (!*ps.p)
		fail(&ps, "empty");
	else
		parse_expr(&ps);
	skip(&ps);
	if (!ps.err && *ps.p)
		fail(&ps, *ps.p == ')' ? "too many ')'" : "unexpected character");
	if (!ps.err && ps.maxdepth > EXPR_MAX_STACK)
		fail(&ps, "expression too deep");
	if (ps.err) {
		*err = ps.err;
		*errpos = ps.errp - src;
		e->n = 0;
		return -1;
	}
	return 0;
}

static ff power(ff a, ff b)
{
	/* Small integer exponents by multiplication: exact for negative bases, and much faster */
	if (ff_lt(ff_abs(b), ff_of(16.5f))) {
		int32_t k = ff_trunc_int(b);
		if (ff_from_int(k) == b || (!k && !(b & ~FF_SIGN))) {
			ff r = FF_ONE;
			int m = k < 0 ? -k : k;
			while (m--)
				r = ff_mul(r, a);
			return k < 0 ? ff_div(FF_ONE, r) : r;
		}
	}
	return ff_pow(a, b);
}

ff expr_eval(const expr_t *e, ff x)
{
	ff st[EXPR_MAX_STACK];
	int sp = 0;
	const expr_insn_t *c = e->code, *end = e->code + e->n;
	if (c == end)
		return FF_NAN;
	for (; c < end; c++) {
		switch (c->op) {
		case OP_CONST: st[sp++] = c->val; break;
		case OP_X:     st[sp++] = x; break;
		case OP_ADD:   sp--; st[sp - 1] = ff_add(st[sp - 1], st[sp]); break;
		case OP_SUB:   sp--; st[sp - 1] = ff_sub(st[sp - 1], st[sp]); break;
		case OP_MUL:   sp--; st[sp - 1] = ff_mul(st[sp - 1], st[sp]); break;
		case OP_DIV:   sp--; st[sp - 1] = ff_div(st[sp - 1], st[sp]); break;
		case OP_POW:   sp--; st[sp - 1] = power(st[sp - 1], st[sp]); break;
		case OP_NEG:   st[sp - 1] = ff_neg(st[sp - 1]); break;
		case OP_SQR:   st[sp - 1] = ff_mul(st[sp - 1], st[sp - 1]); break;
		case OP_FN: {
			ff a = st[sp - 1], t;
			switch (c->fn) {
			case F_SIN:   a = ff_sin(a); break;
			case F_COS:   a = ff_cos(a); break;
			case F_TAN:   a = ff_tan(a); break;
			case F_ASIN:  a = ff_asin(a); break;
			case F_ACOS:  a = ff_acos(a); break;
			case F_ATAN:  a = ff_atan(a); break;
			case F_SINH:
				t = ff_exp(a);
				a = ff_mul(ff_sub(t, ff_div(FF_ONE, t)), ff_of(0.5f));
				break;
			case F_COSH:
				t = ff_exp(a);
				a = ff_mul(ff_add(t, ff_div(FF_ONE, t)), ff_of(0.5f));
				break;
			case F_TANH:
				if (ff_lt(ff_of(9.0f), ff_abs(a)))
					a = (a & FF_SIGN) | FF_ONE;
				else {
					t = ff_exp(ff_add(a, a));
					a = ff_div(ff_sub(t, FF_ONE), ff_add(t, FF_ONE));
				}
				break;
			case F_SQRT:  a = ff_sqrt(a); break;
			case F_ABS:   a = ff_abs(a); break;
			case F_LN:    a = ff_log(a); break;
			case F_LOG:   a = ff_mul(ff_log(a), ff_of(0.43429448f)); break;
			case F_EXP:   a = ff_exp(a); break;
			case F_FLOOR: a = ff_floor(a); break;
			case F_CEIL:  a = ff_ceil(a); break;
			}
			st[sp - 1] = a;
			break;
		}
		}
	}
	return st[0];
}
