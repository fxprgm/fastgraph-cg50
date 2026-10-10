// FastGraph: shared state and screens.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <gint/display.h>
#include "expr.h"

#define NEQ 6
#define TEXT_MAX 60
#define NCOLORS 8
#define WIDTH_MAX 6

typedef struct {
	char text[TEXT_MAX + 1];
	int color;      /* index into palette[] */
	int width;      /* line thickness in pixels, 1..WIDTH_MAX */
	bool on;
	bool ok;        /* compiled without error */
	expr_t code;
	const char *err;
	int errpos;
} equation_t;

typedef struct {
	float xmin, xmax, ymin, ymax;
	bool grid;
	bool legend;
} view_t;

extern equation_t eqs[NEQ];
extern const uint16_t palette[NCOLORS];
extern font_t font_8x13b;

/* Screen layout: 396x224; the graph leaves the bottom rows to the status bar */
#define SCREEN_W 396
#define SCREEN_H 224
#define STATUS_H 18
#define PLOT_H (SCREEN_H - STATUS_H)
#define LINE_H 16

void eq_compile(equation_t *e);
void config_load(void);
void config_save(void);

void list_screen(void);       /* the Y= list; returns when the user asks for the graph */
void graph_screen(view_t *v); /* returns when the user goes back to the list */
void view_reset(view_t *v);

/* "1.25", "-3", "2e-05": a short form for axis labels and the window */
void fmt_num(char *buf, float v);
