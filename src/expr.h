// Expressions in x, compiled once to a small stack program and evaluated per pixel column.
#pragma once
#include <stdint.h>
#include "ff.h"

#define EXPR_MAX_CODE 96
#define EXPR_MAX_STACK 32

typedef struct {
	uint8_t op;
	uint8_t fn;
	ff val;
} expr_insn_t;

typedef struct {
	expr_insn_t code[EXPR_MAX_CODE];
	int n;
} expr_t;

// Compiles src (e.g. "2sin(x)+x^2/3"). Returns 0, or -1 with *err set to a message and *errpos
// to the offset in src where the problem is.
int expr_compile(expr_t *e, const char *src, const char **err, int *errpos);

ff expr_eval(const expr_t *e, ff x);
