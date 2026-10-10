// FastGraph: a function grapher for the fx-CG50 that shows how long each redraw takes.

#include "app.h"
#include "prof.h"
#include <gint/gint.h>
#include <gint/keyboard.h>
#include <gint/drivers/keydev.h>
#include <stdio.h>
#include <string.h>

equation_t eqs[NEQ];

/* Colours that read well on white, in RGB565 (C_RGB takes 0..31 per channel) */
const uint16_t palette[NCOLORS] = {
	C_RGB(0, 8, 28),   /* blue */
	C_RGB(28, 2, 2),   /* red */
	C_RGB(0, 20, 4),   /* green */
	C_RGB(31, 14, 0),  /* orange */
	C_RGB(18, 4, 24),  /* purple */
	C_RGB(0, 18, 20),  /* teal */
	C_RGB(28, 4, 20),  /* magenta */
	C_RGB(0, 0, 0),    /* black */
};

void eq_compile(equation_t *e)
{
	e->err = NULL;
	e->errpos = 0;
	e->ok = e->text[0] && expr_compile(&e->code, e->text, &e->err, &e->errpos) == 0;
}

static void set(int i, const char *text, int color, int width, bool on)
{
	strncpy(eqs[i].text, text, TEXT_MAX);
	eqs[i].color = color;
	eqs[i].width = width;
	eqs[i].on = on;
	eq_compile(&eqs[i]);
}

static void defaults(void)
{
	set(0, "3sin(x)", 0, 2, true);
	set(1, "x^2/4-3", 1, 2, true);
	set(2, "tan(x)", 2, 1, true);
	set(3, "4cos(2x)exp(-x^2/30)", 3, 3, true);
	set(4, "", 4, 2, false);
	set(5, "", 5, 2, false);
}

/* The equations are kept in a text file in the storage memory, one per line:
   "<on> <color> <width> <expression>". BFile calls need the OS, hence the world switch. */
#define CONFIG "/FastGraph.txt"

static int do_load(void)
{
	FILE *f = fopen(CONFIG, "r");
	if (!f)
		return -1;
	char line[TEXT_MAX + 16];
	for (int i = 0; i < NEQ && fgets(line, sizeof line, f); i++) {
		int on, color, width, n = 0;
		if (sscanf(line, "%d %d %d %n", &on, &color, &width, &n) < 3)
			break;
		char *text = line + n;
		text[strcspn(text, "\r\n")] = 0;
		if (color < 0 || color >= NCOLORS)
			color = i % NCOLORS;
		if (width < 1 || width > WIDTH_MAX)
			width = 2;
		set(i, text, color, width, on != 0);
	}
	fclose(f);
	return 0;
}

static int do_save(void)
{
	FILE *f = fopen(CONFIG, "w");
	if (!f)
		return -1;
	for (int i = 0; i < NEQ; i++)
		fprintf(f, "%d %d %d %s\n", eqs[i].on, eqs[i].color, eqs[i].width, eqs[i].text);
	fclose(f);
	return 0;
}

void config_load(void)
{
	gint_world_switch(GINT_CALL(do_load));
}

void config_save(void)
{
	gint_world_switch(GINT_CALL(do_save));
}

int main(void)
{
	prof_init();
	dfont(&font_8x13b);
	keydev_set_standard_repeats(keydev_std(), 300 * 1000, 40 * 1000);
	defaults();
	config_load();

	view_t view;
	view_reset(&view);
	for (;;) {
		list_screen();
		config_save();
		graph_screen(&view);
	}
	prof_quit();
	return 1;
}
