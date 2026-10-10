// The Y= list: six equations with their colour, line width and on/off state, and the editor.

#include "app.h"
#include <gint/keyboard.h>
#include <string.h>

#define TITLE_H 20
#define ROW_Y0 24
#define ROW_H 27
#define FKEY_Y (SCREEN_H - 20)
#define MSG_Y (FKEY_Y - LINE_H - 2)
#define TEXT_X 70
#define TEXT_CHARS 31           /* characters of the expression shown per row */
#define WIDTH_X 330             /* thickness preview */

#define C_TITLE   C_RGB(3, 6, 14)
#define C_SELECT  C_RGB(26, 28, 31)
#define C_GREYTXT C_RGB(16, 16, 16)
#define C_FKEY    C_RGB(6, 8, 12)

static int sel = 0;

static void fkeys(const char *labels[6])
{
	for (int i = 0; i < 6; i++) {
		int x = i * 66;
		drect(x + 1, FKEY_Y, x + 64, SCREEN_H - 1, C_FKEY);
		if (labels[i] && labels[i][0])
			dtext_opt(x + 33, FKEY_Y + 4, C_WHITE, C_NONE, DTEXT_CENTER, DTEXT_TOP, labels[i], -1);
	}
}

/* One row. When editing, buf/cursor/scroll describe the text being edited. */
static void draw_row(int i, bool selected, const char *buf, int cursor, int scroll)
{
	equation_t *e = &eqs[i];
	int y = ROW_Y0 + i * ROW_H;
	uint16_t color = palette[e->color];

	if (selected)
		drect(0, y, SCREEN_W - 1, y + ROW_H - 2, C_SELECT);

	/* colour swatch: filled when on, outlined when off */
	if (e->on)
		drect(6, y + 6, 20, y + 20, color);
	else
		drect_border(6, y + 6, 20, y + 20, C_NONE, 2, color);

	char name[5] = {'Y', '1' + i, '=', 0};
	dtext(28, y + 7, e->on ? C_BLACK : C_GREYTXT, name);

	const char *text = buf ? buf : e->text;
	int len = strlen(text);
	if (!buf && len > TEXT_CHARS)
		scroll = len - TEXT_CHARS;
	char shown[TEXT_CHARS + 1];
	int n = len - scroll < TEXT_CHARS ? len - scroll : TEXT_CHARS;
	memcpy(shown, text + scroll, n);
	shown[n] = 0;
	int tcolor = buf ? C_BLACK : !e->ok && e->text[0] ? C_RED : e->on ? C_BLACK : C_GREYTXT;
	dtext(TEXT_X, y + 7, tcolor, shown);
	if (buf) {
		int cx = TEXT_X + (cursor - scroll) * 8;
		drect(cx - 1, y + 4, cx, y + 21, C_BLUE);
	}

	/* line thickness preview, in the curve's colour */
	int ty = y + 13 - (e->width - 1) / 2;
	drect(WIDTH_X, ty, WIDTH_X + 34, ty + e->width - 1, e->on ? color : C_GREYTXT);
	char w[3] = {'0' + e->width, 0};
	dtext(WIDTH_X + 42, y + 7, C_GREYTXT, w);
}

static void draw(int editing, const char *buf, int cursor, int scroll, const char *msg)
{
	dclear(C_WHITE);
	drect(0, 0, SCREEN_W - 1, TITLE_H - 1, C_TITLE);
	dtext(8, 4, C_WHITE, "FastGraph");
	dtext_opt(SCREEN_W - 8, 4, C_RGB(20, 22, 28), C_NONE, DTEXT_RIGHT, DTEXT_TOP,
		editing ? "EXE: ok  EXIT: cancel" : "EXE: edit  F6: graph", -1);

	for (int i = 0; i < NEQ; i++)
		draw_row(i, i == sel, editing && i == sel ? buf : NULL, cursor, scroll);

	if (msg)
		dtext(8, MSG_Y, C_RED, msg);
	else if (!editing && eqs[sel].err)
		dprint(8, MSG_Y, C_RED, "Y%d: %s (at %d)", sel + 1, eqs[sel].err, eqs[sel].errpos + 1);

	static const char *list_keys[6] = {"COLOR", "WIDTH", "ON/OFF", "CLEAR", "", "DRAW"};
	static const char *edit_keys[6] = {"abs(", "sqrt(", "pi", "e", "exp(", "floor("};
	fkeys(editing ? edit_keys : list_keys);
	dupdate();
}

/* The text a key types in the editor, or NULL */
static const char *key_text(key_event_t ev)
{
	if (ev.shift) {
		switch (ev.key) {
		case KEY_SQUARE: return "sqrt(";
		case KEY_EXP:    return "pi";
		case KEY_LN:     return "e^(";
		case KEY_LOG:    return "10^(";
		case KEY_SIN:    return "asin(";
		case KEY_COS:    return "acos(";
		case KEY_TAN:    return "atan(";
		}
		return NULL;
	}
	switch (ev.key) {
	case KEY_0: return "0";
	case KEY_1: return "1";
	case KEY_2: return "2";
	case KEY_3: return "3";
	case KEY_4: return "4";
	case KEY_5: return "5";
	case KEY_6: return "6";
	case KEY_7: return "7";
	case KEY_8: return "8";
	case KEY_9: return "9";
	case KEY_DOT: return ".";
	case KEY_EXP: return "*10^";
	case KEY_NEG: return "-";
	case KEY_ADD: return "+";
	case KEY_SUB: return "-";
	case KEY_MUL: return "*";
	case KEY_DIV: return "/";
	case KEY_LEFTP: return "(";
	case KEY_RIGHTP: return ")";
	case KEY_XOT: return "x";
	case KEY_SQUARE: return "^2";
	case KEY_POWER: return "^";
	case KEY_SIN: return "sin(";
	case KEY_COS: return "cos(";
	case KEY_TAN: return "tan(";
	case KEY_LN: return "ln(";
	case KEY_LOG: return "log(";
	case KEY_F1: return "abs(";
	case KEY_F2: return "sqrt(";
	case KEY_F3: return "pi";
	case KEY_F4: return "e";
	case KEY_F5: return "exp(";
	case KEY_F6: return "floor(";
	}
	return NULL;
}

/* Edits row sel, starting from text (and typing first if given). */
static void edit(const char *text, const char *first)
{
	char buf[TEXT_MAX + 1];
	strcpy(buf, text);
	int cursor = strlen(buf), scroll = 0;
	const char *msg = NULL;

	for (;;) {
		if (first) {
			int n = strlen(first);
			if (strlen(buf) + n <= TEXT_MAX) {
				memmove(buf + cursor + n, buf + cursor, strlen(buf + cursor) + 1);
				memcpy(buf + cursor, first, n);
				cursor += n;
			}
			first = NULL;
		}
		if (cursor < scroll)
			scroll = cursor;
		if (cursor > scroll + TEXT_CHARS - 1)
			scroll = cursor - TEXT_CHARS + 1;
		draw(1, buf, cursor, scroll, msg);
		msg = NULL;

		key_event_t ev = getkey_opt(GETKEY_DEFAULT | GETKEY_REP_ALL, NULL);
		const char *t = key_text(ev);
		if (t) {
			first = t;
			continue;
		}
		switch (ev.key) {
		case KEY_LEFT:
			if (cursor > 0) cursor--;
			break;
		case KEY_RIGHT:
			if (cursor < (int)strlen(buf)) cursor++;
			break;
		case KEY_UP:
			cursor = 0;
			break;
		case KEY_DOWN:
			cursor = strlen(buf);
			break;
		case KEY_DEL:
			if (cursor > 0) {
				memmove(buf + cursor - 1, buf + cursor, strlen(buf + cursor) + 1);
				cursor--;
			}
			break;
		case KEY_ACON:
			buf[0] = 0;
			cursor = scroll = 0;
			break;
		case KEY_EXIT:
			return;
		case KEY_EXE: {
			equation_t *e = &eqs[sel];
			strcpy(e->text, buf);
			eq_compile(e);
			if (e->text[0] && !e->ok) {
				cursor = e->errpos;
				msg = e->err;
				break;
			}
			e->on = e->text[0] != 0;
			return;
		}
		}
	}
}

void list_screen(void)
{
	for (;;) {
		draw(0, NULL, 0, 0, NULL);
		key_event_t ev = getkey_opt(GETKEY_DEFAULT, NULL);
		equation_t *e = &eqs[sel];
		const char *t = key_text(ev);
		/* typing on a row starts a new expression; F-keys are the list's own here */
		if (t && !(ev.key >= KEY_F1 && ev.key <= KEY_F6)) {
			edit("", t);
			continue;
		}
		switch (ev.key) {
		case KEY_UP:
			sel = (sel + NEQ - 1) % NEQ;
			break;
		case KEY_DOWN:
			sel = (sel + 1) % NEQ;
			break;
		case KEY_EXE:
			edit(e->text, NULL);
			break;
		case KEY_F1:
			e->color = (e->color + (ev.shift ? NCOLORS - 1 : 1)) % NCOLORS;
			break;
		case KEY_F2:
		case KEY_RIGHT:
			e->width = e->width % WIDTH_MAX + 1;
			break;
		case KEY_LEFT:
			e->width = e->width == 1 ? WIDTH_MAX : e->width - 1;
			break;
		case KEY_F3:
			e->on = !e->on && e->ok;
			break;
		case KEY_F4:
		case KEY_DEL:
			e->text[0] = 0;
			e->on = false;
			eq_compile(e);
			break;
		case KEY_F6:
			return;
		}
	}
}
