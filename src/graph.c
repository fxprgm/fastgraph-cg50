// The graph: grid, axes, curves, and the time each redraw takes.

#include "app.h"
#include "prof.h"
#include "ff.h"
#include <gint/keyboard.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define C_GRID    C_RGB(27, 27, 28)
#define C_AXIS    C_RGB(8, 8, 10)
#define C_LABEL   C_RGB(12, 12, 14)
#define C_STATUS  C_RGB(3, 6, 14)
#define C_STATTXT C_RGB(22, 26, 31)

typedef struct {
	uint32_t plot_us;   /* grid, axes and curves into VRAM */
	uint32_t lcd_us;    /* VRAM to the display */
	uint32_t grid_us;   /* background, grid, axes and labels */
	uint32_t eq_us[NEQ];/* each curve */
	int points;         /* expression evaluations */
} timing_t;

void view_reset(view_t *v)
{
	/* square pixels: the y range follows from the x range and the plot's shape */
	v->xmin = -10;
	v->xmax = 10;
	float h = 20.0f * PLOT_H / SCREEN_W;
	v->ymin = -h / 2;
	v->ymax = h / 2;
	v->grid = true;
	v->legend = true;
}

void fmt_num(char *buf, float v)
{
	float a = fabsf(v);
	if (a < 1e-6f) {
		strcpy(buf, "0");
		return;
	}
	if (a >= 1e5f || a < 1e-3f) {
		int ex = (int)floorf(log10f(a));
		float m = v / powf(10, ex);
		int mi = (int)roundf(m * 10);
		sprintf(buf, "%d.%de%d", mi / 10, (mi < 0 ? -mi : mi) % 10, ex);
		return;
	}
	/* up to 3 decimals, trailing zeros removed */
	int neg = v < 0;
	long k = lroundf(a * 1000);
	char *p = buf;
	if (neg)
		*p++ = '-';
	p += sprintf(p, "%ld", k / 1000);
	int frac = k % 1000;
	if (frac) {
		char d[5];
		sprintf(d, ".%03d", frac);
		int n = 4;
		while (d[n - 1] == '0')
			n--;
		d[n] = 0;
		strcpy(p, d);
	}
}

/* A tick spacing of 1, 2 or 5 x 10^k with at least min_px pixels between ticks */
static float nice_step(float range, int pixels, int min_px)
{
	float raw = range * min_px / pixels;
	float p = powf(10, floorf(log10f(raw)));
	float m = raw / p;
	return (m <= 1 ? 1 : m <= 2 ? 2 : m <= 5 ? 5 : 10) * p;
}

static void clip_rect(int x1, int y1, int x2, int y2, int color)
{
	if (x1 < 0) x1 = 0;
	if (y1 < 0) y1 = 0;
	if (x2 >= SCREEN_W) x2 = SCREEN_W - 1;
	if (y2 >= PLOT_H) y2 = PLOT_H - 1;
	if (x1 <= x2 && y1 <= y2)
		drect(x1, y1, x2, y2, color);
}

static void grid_and_axes(const view_t *v)
{
	float sx = SCREEN_W / (v->xmax - v->xmin), sy = PLOT_H / (v->ymax - v->ymin);
	float stx = nice_step(v->xmax - v->xmin, SCREEN_W, 44);
	float sty = nice_step(v->ymax - v->ymin, PLOT_H, 34);
	int ax = (int)lroundf(-v->xmin * sx);   /* x = 0 */
	int ay = (int)lroundf(v->ymax * sy);    /* y = 0 */

	if (v->grid) {
		for (float x = ceilf(v->xmin / stx) * stx; x <= v->xmax; x += stx)
			clip_rect(lroundf((x - v->xmin) * sx), 0, lroundf((x - v->xmin) * sx), PLOT_H - 1, C_GRID);
		for (float y = ceilf(v->ymin / sty) * sty; y <= v->ymax; y += sty)
			clip_rect(0, lroundf((v->ymax - y) * sy), SCREEN_W - 1, lroundf((v->ymax - y) * sy), C_GRID);
	}
	clip_rect(ax, 0, ax, PLOT_H - 1, C_AXIS);
	clip_rect(0, ay, SCREEN_W - 1, ay, C_AXIS);

	/* labels in gint's small font, along the axes (or the edges when an axis is off screen) */
	font_t const *old = dfont(NULL);
	char buf[16];
	int ly = ay < 0 ? 2 : ay > PLOT_H - 12 ? PLOT_H - 12 : ay + 3;
	for (float x = ceilf(v->xmin / stx) * stx; x <= v->xmax; x += stx) {
		if (fabsf(x) < stx / 2)
			continue;
		int px = lroundf((x - v->xmin) * sx);
		clip_rect(px, ay - 2, px, ay + 2, C_AXIS);
		fmt_num(buf, x);
		dtext_opt(px, ly, C_LABEL, C_NONE, DTEXT_CENTER, DTEXT_TOP, buf, -1);
	}
	int lx = ax < 0 ? 3 : ax > SCREEN_W - 40 ? SCREEN_W - 3 : ax + 4;
	int halign = ax > SCREEN_W - 40 ? DTEXT_RIGHT : DTEXT_LEFT;
	for (float y = ceilf(v->ymin / sty) * sty; y <= v->ymax; y += sty) {
		if (fabsf(y) < sty / 2)
			continue;
		int py = lroundf((v->ymax - y) * sy);
		if (py < 6 || py > PLOT_H - 6)
			continue;
		clip_rect(ax - 2, py, ax + 2, py, C_AXIS);
		fmt_num(buf, y);
		dtext_opt(lx, py, C_LABEL, C_NONE, halign, DTEXT_MIDDLE, buf, -1);
	}
	dfont(old);
}

/* A vertical span y1..y2 at column x, drawn with a square brush covering [x-lo, x+hi] and
   [y-lo, y+hi], straight into VRAM and clipped to the plot */
static inline void span(uint16_t *vram, int x, int y1, int y2, int lo, int hi, uint16_t color)
{
	int xa = x - lo, xb = x + hi;
	int ya = (y1 < y2 ? y1 : y2) - lo, yb = (y1 < y2 ? y2 : y1) + hi;
	if (xa < 0) xa = 0;
	if (xb >= SCREEN_W) xb = SCREEN_W - 1;
	if (ya < 0) ya = 0;
	if (yb >= PLOT_H) yb = PLOT_H - 1;
	for (int y = ya; y <= yb; y++) {
		uint16_t *p = vram + y * SCREEN_W;
		for (int c = xa; c <= xb; c++)
			p[c] = color;
	}
}

/* x of each pixel column's centre, shared by all curves of a frame */
static ff xs[SCREEN_W];

static void columns(const view_t *v)
{
	float dx = (v->xmax - v->xmin) / SCREEN_W;
	ff x0 = ff_of(v->xmin + dx / 2), d = ff_of(dx);
	for (int px = 0; px < SCREEN_W; px++)
		xs[px] = ff_add(x0, ff_mul(ff_from_int(px), d));
}

/* One curve, sampled once per pixel column. Consecutive samples are joined by vertical spans
   (half in each column), drawn with a square brush of the curve's width: for a function graph
   that is a correct thick line. */
static int curve(const view_t *v, const equation_t *e)
{
	ff ymax = ff_of(v->ymax), sy = ff_of(PLOT_H / (v->ymax - v->ymin));
	int lo = (e->width - 1) / 2, hi = e->width - 1 - lo;
	uint16_t color = palette[e->color];
	uint16_t *vram = gint_vram;
	bool have = false;
	int prev = 0;

	for (int px = 0; px < SCREEN_W; px++) {
		ff y = expr_eval(&e->code, xs[px]);
		if (!ff_finite(y)) {
			have = false;
			continue;
		}
		int py = ff_to_int_clamp(ff_mul(ff_sub(ymax, y), sy), -2 * PLOT_H, 3 * PLOT_H);
		/* A segment crossing the whole plot may be a pole (tan, 1/x) rather than a steep curve:
		   then the value halfway between the columns is not between the two ends. */
		bool jump = false;
		if (have && ((prev < 0 && py >= PLOT_H) || (py < 0 && prev >= PLOT_H))) {
			ff ym = expr_eval(&e->code, ff_mul(ff_add(xs[px - 1], xs[px]), ff_of(0.5f)));
			int pm = ff_finite(ym) ? ff_to_int_clamp(ff_mul(ff_sub(ymax, ym), sy), -4 * PLOT_H, 5 * PLOT_H) : -9 * PLOT_H;
			int a = prev < py ? prev : py, b = prev < py ? py : prev;
			jump = pm < a || pm > b;
		}
		if (have && !jump) {
			int mid = (prev + py) / 2;
			span(vram, px - 1, prev, mid, lo, hi, color);
			span(vram, px, mid, py, lo, hi, color);
		}
		else
			span(vram, px, py, py, lo, hi, color);
		prev = py;
		have = true;
	}
	return SCREEN_W;
}

static void legend(void)
{
	int y = 4;
	for (int i = 0; i < NEQ; i++) {
		equation_t *e = &eqs[i];
		if (!e->on || !e->ok)
			continue;
		char buf[TEXT_MAX + 8];
		sprintf(buf, "Y%d=%s", i + 1, e->text);
		int w, h;
		dsize(buf, NULL, &w, &h);
		drect(4, y - 1, 4 + 18 + w + 4, y + LINE_H - 2, C_WHITE);
		drect(6, y + 4, 18, y + 8, palette[e->color]);
		dtext(22, y, palette[e->color], buf);
		y += LINE_H;
	}
}

/* Everything above the status bar; returns its timing */
static void render(const view_t *v, timing_t *t)
{
	uint32_t t0 = prof_now();
	dclear(C_WHITE);
	grid_and_axes(v);
	columns(v);
	uint32_t t1 = prof_now();
	t->grid_us = prof_us(t0, t1);
	t->points = 0;
	for (int i = 0; i < NEQ; i++) {
		t->eq_us[i] = 0;
		if (!eqs[i].on || !eqs[i].ok)
			continue;
		t->points += curve(v, &eqs[i]);
		uint32_t t2 = prof_now();
		t->eq_us[i] = prof_us(t1, t2);
		t1 = t2;
	}
	if (v->legend)
		legend();
	t->plot_us = prof_us(t0, prof_now());
}

static void fmt_ms(char *buf, uint32_t us)
{
	sprintf(buf, "%d.%02d", (int)(us / 1000), (int)(us % 1000 / 10));
}

static void status(const timing_t *t)
{
	char p[16], l[16];
	fmt_ms(p, t->plot_us);
	fmt_ms(l, t->lcd_us);
	uint32_t frame = t->plot_us + t->lcd_us;
	int fps = frame ? 1000000 / frame : 0;
	drect(0, PLOT_H, SCREEN_W - 1, SCREEN_H - 1, C_STATUS);
	dprint(4, PLOT_H + 3, C_WHITE, "plot %s ms  lcd %s ms  %d fps", p, l, fps);
	dtext_opt(SCREEN_W - 4, PLOT_H + 3, C_STATTXT, C_NONE, DTEXT_RIGHT, DTEXT_TOP, "F4 bench", -1);
}

/* F4: redraw many times and report the averages */
static void benchmark(const view_t *v)
{
	enum { N = 60, M = 30 };
	timing_t t;
	uint32_t sum = 0, best = 0xffffffff, grid = 0, eq[NEQ] = {0};
	int points = 0;
	for (int i = 0; i < N; i++) {
		render(v, &t);
		sum += t.plot_us;
		if (t.plot_us < best)
			best = t.plot_us;
		points = t.points;
		grid += t.grid_us;
		for (int k = 0; k < NEQ; k++)
			eq[k] += t.eq_us[k];
	}
	uint32_t t0 = prof_now();
	for (int i = 0; i < M; i++) {
		render(v, &t);
		dupdate();
	}
	uint32_t frame = prof_us(t0, prof_now()) / M;

	char avg[16], mn[16], fr[16];
	fmt_ms(avg, sum / N);
	fmt_ms(mn, best);
	fmt_ms(fr, frame);
	uint32_t evals = sum ? (uint64_t)points * N * 1000000 / sum : 0;

	int shown = 0;
	for (int k = 0; k < NEQ; k++)
		shown += eqs[k].on && eqs[k].ok;
	int x1 = 24, y1 = 12, x2 = SCREEN_W - 24, y2 = y1 + (8 + shown) * LINE_H + 12;
	drect_border(x1, y1, x2, y2, C_WHITE, 2, C_STATUS);
	int y = y1 + 6;
	dtext(x1 + 10, y, C_STATUS, "Benchmark");
	y += LINE_H + 4;
	dprint(x1 + 10, y, C_BLACK, "plot   avg %s ms, best %s", avg, mn);
	y += LINE_H;
	dprint(x1 + 10, y, C_BLACK, "frame  %s ms with LCD = %d fps", fr, frame ? (int)(1000000 / frame) : 0);
	y += LINE_H;
	dprint(x1 + 10, y, C_BLACK, "points %d per frame", points);
	y += LINE_H;
	dprint(x1 + 10, y, C_BLACK, "       %d evaluations/s", (int)evals);
	y += LINE_H;
	fmt_ms(avg, grid / N);
	dprint(x1 + 10, y, C_BLACK, "grid   %s ms", avg);
	y += LINE_H;
	for (int k = 0; k < NEQ; k++) {
		if (!eqs[k].on || !eqs[k].ok)
			continue;
		fmt_ms(avg, eq[k] / N);
		char name[24];
		snprintf(name, sizeof name, "%.15s", eqs[k].text);
		dprint(x1 + 10, y, palette[eqs[k].color], "Y%d     %s ms  %s", k + 1, avg, name);
		y += LINE_H;
	}
	y += 4;
	dtext(x1 + 10, y, C_LABEL, "any key: back");
	dupdate();
	getkey();
}

void graph_screen(view_t *v)
{
	timing_t t = {0};
	for (;;) {
		render(v, &t);
		status(&t);
		uint32_t t0 = prof_now();
		dupdate();
		t.lcd_us = prof_us(t0, prof_now());

		key_event_t ev = getkey_opt(GETKEY_DEFAULT | GETKEY_REP_ALL, NULL);
		float w = v->xmax - v->xmin, h = v->ymax - v->ymin;
		float cx = (v->xmin + v->xmax) / 2, cy = (v->ymin + v->ymax) / 2;
		switch (ev.key) {
		case KEY_LEFT:  v->xmin -= w / 10; v->xmax -= w / 10; break;
		case KEY_RIGHT: v->xmin += w / 10; v->xmax += w / 10; break;
		case KEY_UP:    v->ymin += h / 10; v->ymax += h / 10; break;
		case KEY_DOWN:  v->ymin -= h / 10; v->ymax -= h / 10; break;
		case KEY_ADD:
		case KEY_SUB: {
			float f = ev.key == KEY_ADD ? 1 / 1.5f : 1.5f;
			v->xmin = cx - w * f / 2;
			v->xmax = cx + w * f / 2;
			v->ymin = cy - h * f / 2;
			v->ymax = cy + h * f / 2;
			break;
		}
		case KEY_F1: view_reset(v); break;
		case KEY_F2: v->grid = !v->grid; break;
		case KEY_F3: v->legend = !v->legend; break;
		case KEY_F4: benchmark(v); break;
		case KEY_EXIT:
		case KEY_F6:
			return;
		}
	}
}
