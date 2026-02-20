/* See LICENSE file for copyright and license details. */
#include <curses.h>
#include <ctype.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"
#include "eval.h"
#include "config.h"

#define CELLTEXT  256
#define HEADERW   4      /* row header width */
#define MAX(A, B) ((A) > (B) ? (A) : (B))
#define MIN(A, B) ((A) < (B) ? (A) : (B))

/* typedefs */
typedef struct {
	char text[CELLTEXT]; /* raw text / formula */
	double val;          /* computed numeric value */
	int hasval;          /* 1 if val is valid */
} Cell;

/* enums */
enum { ModeNormal, ModeEdit, ModeCommand };

/* globals */
static Cell *cells;      /* flat array: cells[row * maxcols + col] */
static char filename[512];
static int dirty;        /* unsaved changes flag */
static int crow, ccol;   /* cursor row, col */
static int vrow, vcol;   /* viewport top-left row, col */
static int mode;         /* current input mode */
static char editbuf[CELLTEXT]; /* edit buffer */
static int editlen;      /* edit buffer length */
static int editpos;      /* cursor position in edit buffer */
static char cmdbuf[CELLTEXT]; /* command buffer */
static int cmdlen;
static char yankbuf[CELLTEXT]; /* yank buffer */
static char statusmsg[256]; /* status message */
static int running;
char *argv0;

/* macros */
#define CELL(r, c) (&cells[(r) * maxcols + (c)])

/* forward declarations */
static int celladdr(const char *s, int *row, int *col);
static void recalc(void);
static void draw(void);

/* callback for eval.c to get cell values by address */
static double
cellvalfn(const char *addr, int *ok)
{
	int r, c;

	if (!celladdr(addr, &r, &c)) {
		*ok = 0;
		return 0;
	}
	*ok = CELL(r, c)->hasval;
	return CELL(r, c)->val;
}

static void
initcells(void)
{
	cells = ecalloc(maxrows * maxcols, sizeof(Cell));
	eval_setcellfn(cellvalfn);
}

/* parse column name to index: A=0, B=1, ..., Z=25 */
static int
colname2idx(const char *s, const char **end)
{
	int c = 0;

	if (!s || !isupper((unsigned char)*s))
		return -1;
	while (isupper((unsigned char)*s)) {
		c = c * 26 + (*s - 'A' + 1);
		s++;
	}
	if (end)
		*end = s;
	return c - 1;
}

/* parse row number (1-based) to index: "1"=0, "2"=1 */
static int
rowstr2idx(const char *s, const char **end)
{
	int r = 0;

	if (!s || !isdigit((unsigned char)*s))
		return -1;
	while (isdigit((unsigned char)*s)) {
		r = r * 10 + (*s - '0');
		s++;
	}
	if (end)
		*end = s;
	return r - 1;
}

/* parse cell address like "A1" into row, col indices */
static int
celladdr(const char *s, int *row, int *col)
{
	const char *p = s;
	int c, r;

	c = colname2idx(p, &p);
	if (c < 0 || c >= maxcols)
		return 0;
	r = rowstr2idx(p, &p);
	if (r < 0 || r >= maxrows)
		return 0;
	*col = c;
	*row = r;
	return 1;
}

/* format column name from index: 0=A, 1=B, ..., 25=Z */
static void
colname(int c, char *buf, int bufsz)
{
	if (c < 26)
		snprintf(buf, bufsz, "%c", 'A' + c);
	else
		snprintf(buf, bufsz, "%c%c", 'A' + c / 26 - 1, 'A' + c % 26);
}

/* get display value of a cell as string */
static void
celldisp(int row, int col, char *buf, int bufsz)
{
	Cell *c = CELL(row, col);

	if (c->text[0] == '\0') {
		buf[0] = '\0';
		return;
	}
	if (c->hasval)
		snprintf(buf, bufsz, "%g", c->val);
	else
		snprintf(buf, bufsz, "%.*s", bufsz - 1, c->text);
}

/* set a cell's raw text */
static void
cellset(int row, int col, const char *text)
{
	Cell *c = CELL(row, col);

	snprintf(c->text, CELLTEXT, "%s", text);
	c->hasval = 0;
	c->val = 0;
	dirty = 1;
}

/* clear a cell */
static void
cellclear(int row, int col)
{
	Cell *c = CELL(row, col);

	memset(c, 0, sizeof(Cell));
	dirty = 1;
}

/* recalculate all cells */
static void
recalc(void)
{
	int r, c;
	Cell *cell;

	for (r = 0; r < maxrows; r++) {
		for (c = 0; c < maxcols; c++) {
			cell = CELL(r, c);
			if (cell->text[0] == '=') {
				cell->val = eval_expr(cell->text + 1);
				cell->hasval = 1;
			} else if (cell->text[0] != '\0') {
				char *end;
				double v = strtod(cell->text, &end);
				if (*end == '\0' && end != cell->text) {
					cell->val = v;
					cell->hasval = 1;
				} else {
					cell->hasval = 0;
				}
			}
		}
	}
}

/* read CSV file into cells */
static void
readcsv(const char *path)
{
	FILE *fp;
	char line[8192];
	int row = 0;

	if (!(fp = fopen(path, "r")))
		return;

	while (fgets(line, sizeof(line), fp) && row < maxrows) {
		char *p = line;
		int col = 0;
		char *start;

		/* strip trailing newline */
		p[strcspn(p, "\n")] = '\0';

		while (*p && col < maxcols) {
			start = p;

			if (*p == '"') {
				start = ++p;
				while (*p && !(*p == '"' && (*(p+1) == separator || *(p+1) == '\0')))
					p++;
				if (*p == '"')
					*p++ = '\0';
			} else {
				while (*p && *p != separator)
					p++;
			}
			if (*p == separator)
				*p++ = '\0';

			if (*start)
				cellset(row, col, start);
			col++;
		}
		row++;
	}
	fclose(fp);
	dirty = 0;
}

/* write cells to CSV file */
static void
writecsv(const char *path)
{
	FILE *fp;
	int r, c, lastrow = 0, lastcol;

	/* find extent of data */
	for (r = 0; r < maxrows; r++)
		for (c = 0; c < maxcols; c++)
			if (CELL(r, c)->text[0])
				lastrow = r + 1;

	if (!(fp = fopen(path, "w")))
		die("cannot write %s:", path);

	for (r = 0; r < lastrow; r++) {
		lastcol = 0;
		for (c = 0; c < maxcols; c++)
			if (CELL(r, c)->text[0])
				lastcol = c + 1;

		for (c = 0; c < lastcol; c++) {
			Cell *cell = CELL(r, c);
			if (c > 0)
				fputc(separator, fp);
			if (strchr(cell->text, separator) || strchr(cell->text, '"')) {
				fputc('"', fp);
				for (char *p = cell->text; *p; p++) {
					if (*p == '"')
						fputc('"', fp);
					fputc(*p, fp);
				}
				fputc('"', fp);
			} else {
				fputs(cell->text, fp);
			}
		}
		fputc('\n', fp);
	}
	fclose(fp);
	dirty = 0;
}

/* ensure cursor is visible in viewport */
static void
scrollview(void)
{
	int viscols, visrows;

	visrows = LINES - 2; /* header row + status bar */
	viscols = (COLS - HEADERW) / colwidth;

	if (crow < vrow)
		vrow = crow;
	if (crow >= vrow + visrows)
		vrow = crow - visrows + 1;
	if (ccol < vcol)
		vcol = ccol;
	if (ccol >= vcol + viscols)
		vcol = ccol - viscols + 1;
}

/* draw the spreadsheet grid */
static void
draw(void)
{
	int r, c, x, y;
	int visrows, viscols;
	char buf[CELLTEXT];
	char cn[8];

	visrows = LINES - 2;
	viscols = (COLS - HEADERW) / colwidth;

	erase();

	/* column headers */
	attron(A_BOLD);
	for (c = 0; c < viscols && vcol + c < maxcols; c++) {
		x = HEADERW + c * colwidth;
		colname(vcol + c, cn, sizeof(cn));
		mvprintw(0, x, "%-*.*s", colwidth, colwidth, cn);
	}

	/* row headers and cells */
	for (r = 0; r < visrows && vrow + r < maxrows; r++) {
		y = r + 1;

		/* row header */
		attron(A_BOLD);
		mvprintw(y, 0, "%*d", HEADERW - 1, vrow + r + 1);
		attroff(A_BOLD);

		/* cells */
		for (c = 0; c < viscols && vcol + c < maxcols; c++) {
			x = HEADERW + c * colwidth;
			celldisp(vrow + r, vcol + c, buf, colwidth + 1);

			if (vrow + r == crow && vcol + c == ccol)
				attron(A_REVERSE);
			mvprintw(y, x, "%-*.*s", colwidth, colwidth, buf);
			if (vrow + r == crow && vcol + c == ccol)
				attroff(A_REVERSE);
		}
	}
	attroff(A_BOLD);

	/* status bar */
	attron(A_REVERSE);
	colname(ccol, cn, sizeof(cn));
	if (mode == ModeEdit) {
		mvprintw(LINES - 1, 0, " %s%d: %s", cn, crow + 1, editbuf);
		/* pad to end of line */
		for (x = getcurx(stdscr); x < COLS; x++)
			addch(' ');
	} else if (mode == ModeCommand) {
		mvprintw(LINES - 1, 0, ":%s", cmdbuf);
		for (x = getcurx(stdscr); x < COLS; x++)
			addch(' ');
	} else {
		Cell *cell = CELL(crow, ccol);
		mvprintw(LINES - 1, 0, " %s%d%s | %s",
			cn, crow + 1,
			dirty ? " [+]" : "",
			cell->text);
		if (statusmsg[0]) {
			int slen = strlen(statusmsg);
			mvprintw(LINES - 1, COLS - slen - 1, "%s", statusmsg);
		}
		for (x = getcurx(stdscr); x < COLS; x++)
			addch(' ');
	}
	attroff(A_REVERSE);

	/* position cursor */
	if (mode == ModeEdit)
		move(LINES - 1, (int)strlen(cn) + 4 + editpos);
	else if (mode == ModeCommand)
		move(LINES - 1, 1 + cmdlen);
	else
		move(crow - vrow + 1, HEADERW + (ccol - vcol) * colwidth);

	refresh();
}

/* enter edit mode */
static void
editenter(int clear)
{
	mode = ModeEdit;
	if (clear) {
		editbuf[0] = '\0';
		editlen = 0;
	} else {
		snprintf(editbuf, CELLTEXT, "%s", CELL(crow, ccol)->text);
		editlen = strlen(editbuf);
	}
	editpos = editlen;
	statusmsg[0] = '\0';
}

/* confirm edit */
static void
editconfirm(void)
{
	cellset(crow, ccol, editbuf);
	recalc();
	mode = ModeNormal;
}

/* cancel edit */
static void
editcancel(void)
{
	mode = ModeNormal;
	statusmsg[0] = '\0';
}

/* handle key in edit mode */
static void
editkey(int ch)
{
	switch (ch) {
	case '\n':
	case '\r':
	case KEY_ENTER:
		editconfirm();
		/* move down after enter */
		if (crow < maxrows - 1)
			crow++;
		scrollview();
		break;
	case 27: /* escape */
		editcancel();
		break;
	case KEY_BACKSPACE:
	case 127:
	case 8:
		if (editpos > 0) {
			memmove(editbuf + editpos - 1, editbuf + editpos,
				editlen - editpos + 1);
			editpos--;
			editlen--;
		}
		break;
	case KEY_DC:
		if (editpos < editlen) {
			memmove(editbuf + editpos, editbuf + editpos + 1,
				editlen - editpos);
			editlen--;
		}
		break;
	case KEY_LEFT:
		if (editpos > 0)
			editpos--;
		break;
	case KEY_RIGHT:
		if (editpos < editlen)
			editpos++;
		break;
	case KEY_HOME:
	case 1: /* ctrl-a */
		editpos = 0;
		break;
	case KEY_END:
	case 5: /* ctrl-e */
		editpos = editlen;
		break;
	case 21: /* ctrl-u */
		editbuf[0] = '\0';
		editlen = 0;
		editpos = 0;
		break;
	default:
		if (ch >= 32 && ch < 127 && editlen < CELLTEXT - 1) {
			memmove(editbuf + editpos + 1, editbuf + editpos,
				editlen - editpos + 1);
			editbuf[editpos] = ch;
			editpos++;
			editlen++;
		}
		break;
	}
}

/* execute command */
static void
runcmd(const char *cmd)
{
	int r, c;

	if (cmd[0] == 'q') {
		if (dirty && cmd[1] != '!') {
			snprintf(statusmsg, sizeof(statusmsg),
				"unsaved changes (use :q! to force)");
			return;
		}
		running = 0;
	} else if (cmd[0] == 'w') {
		if (cmd[1] == ' ' && cmd[2])
			snprintf(filename, sizeof(filename), "%s", cmd + 2);
		if (!filename[0]) {
			snprintf(statusmsg, sizeof(statusmsg), "no filename");
			return;
		}
		writecsv(filename);
		snprintf(statusmsg, sizeof(statusmsg), "wrote %s", filename);
	} else if (strcmp(cmd, "wq") == 0) {
		if (!filename[0]) {
			snprintf(statusmsg, sizeof(statusmsg), "no filename");
			return;
		}
		writecsv(filename);
		running = 0;
	} else if (celladdr(cmd, &r, &c)) {
		/* goto cell address */
		crow = r;
		ccol = c;
		scrollview();
	} else {
		snprintf(statusmsg, sizeof(statusmsg), "unknown command: %s", cmd);
	}
}

/* handle key in command mode */
static void
cmdkey(int ch)
{
	switch (ch) {
	case '\n':
	case '\r':
	case KEY_ENTER:
		cmdbuf[cmdlen] = '\0';
		runcmd(cmdbuf);
		mode = ModeNormal;
		break;
	case 27: /* escape */
		mode = ModeNormal;
		statusmsg[0] = '\0';
		break;
	case KEY_BACKSPACE:
	case 127:
	case 8:
		if (cmdlen > 0)
			cmdbuf[--cmdlen] = '\0';
		else
			mode = ModeNormal;
		break;
	default:
		if (ch >= 32 && ch < 127 && cmdlen < CELLTEXT - 1)
			cmdbuf[cmdlen++] = ch;
		break;
	}
}

/* handle key in normal mode */
static void
normalkey(int ch)
{
	statusmsg[0] = '\0';

	switch (ch) {
	case 'q':
		if (dirty) {
			snprintf(statusmsg, sizeof(statusmsg),
				"unsaved changes (use :q! to force)");
		} else {
			running = 0;
		}
		break;
	case 'h':
	case KEY_LEFT:
		if (ccol > 0)
			ccol--;
		scrollview();
		break;
	case 'j':
	case KEY_DOWN:
		if (crow < maxrows - 1)
			crow++;
		scrollview();
		break;
	case 'k':
	case KEY_UP:
		if (crow > 0)
			crow--;
		scrollview();
		break;
	case 'l':
	case KEY_RIGHT:
		if (ccol < maxcols - 1)
			ccol++;
		scrollview();
		break;
	case '\t': /* tab: move right */
		if (ccol < maxcols - 1)
			ccol++;
		scrollview();
		break;
	case KEY_BTAB: /* shift-tab: move left */
		if (ccol > 0)
			ccol--;
		scrollview();
		break;
	case 'g': /* go to top-left */
		crow = 0;
		ccol = 0;
		scrollview();
		break;
	case 'G': /* go to last used row */
		{
			int r, c;
			int lastrow = 0;
			for (r = 0; r < maxrows; r++)
				for (c = 0; c < maxcols; c++)
					if (CELL(r, c)->text[0])
						lastrow = r;
			crow = lastrow;
			scrollview();
		}
		break;
	case '0':
	case KEY_HOME:
		ccol = 0;
		scrollview();
		break;
	case '$':
	case KEY_END:
		{
			int c;
			int lastcol = 0;
			for (c = 0; c < maxcols; c++)
				if (CELL(crow, c)->text[0])
					lastcol = c;
			ccol = lastcol;
			scrollview();
		}
		break;
	case KEY_PPAGE: /* page up */
		crow -= LINES - 3;
		if (crow < 0)
			crow = 0;
		scrollview();
		break;
	case KEY_NPAGE: /* page down */
		crow += LINES - 3;
		if (crow >= maxrows)
			crow = maxrows - 1;
		scrollview();
		break;
	case '\n':
	case '\r':
	case KEY_ENTER:
		editenter(0);
		break;
	case 'i': /* edit cell (clear) */
	case '=': /* start formula */
		if (ch == '=') {
			editenter(1);
			editbuf[0] = '=';
			editlen = 1;
			editpos = 1;
		} else {
			editenter(1);
		}
		break;
	case 'e': /* edit cell (keep content) */
		editenter(0);
		break;
	case 'x': /* delete cell */
	case KEY_DC:
		snprintf(yankbuf, CELLTEXT, "%s", CELL(crow, ccol)->text);
		cellclear(crow, ccol);
		recalc();
		break;
	case 'y': /* yank cell */
		snprintf(yankbuf, CELLTEXT, "%s", CELL(crow, ccol)->text);
		snprintf(statusmsg, sizeof(statusmsg), "yanked");
		break;
	case 'p': /* paste */
		if (yankbuf[0]) {
			cellset(crow, ccol, yankbuf);
			recalc();
		}
		break;
	case ':': /* command mode */
		mode = ModeCommand;
		cmdbuf[0] = '\0';
		cmdlen = 0;
		break;
	case 19: /* ctrl-s: save */
		if (!filename[0]) {
			snprintf(statusmsg, sizeof(statusmsg), "no filename");
		} else {
			writecsv(filename);
			snprintf(statusmsg, sizeof(statusmsg), "wrote %s", filename);
		}
		break;
	default:
		/* start typing into cell directly if printable */
		if (ch >= '0' && ch <= '9') {
			editenter(1);
			editbuf[0] = ch;
			editlen = 1;
			editpos = 1;
		}
		break;
	}
}

static void
cleanup(void)
{
	endwin();
}

static void
sighandler(int sig)
{
	(void)sig;
	cleanup();
	exit(1);
}

static void
usage(void)
{
	die("usage: sheets [-v] [file]");
}

static void
run(void)
{
	int ch;

	running = 1;
	while (running) {
		draw();
		ch = getch();
		if (ch == ERR)
			continue;
		switch (mode) {
		case ModeEdit:
			editkey(ch);
			break;
		case ModeCommand:
			cmdkey(ch);
			break;
		default:
			normalkey(ch);
			break;
		}
	}
}

static void
initui(void)
{
	initscr();
	raw();
	noecho();
	nonl();
	keypad(stdscr, TRUE);
	curs_set(1);

	if (has_colors()) {
		start_color();
		use_default_colors();
		init_pair(ColorNorm, -1, -1);
		init_pair(ColorSel, COLOR_WHITE, COLOR_BLUE);
		init_pair(ColorHeader, COLOR_YELLOW, -1);
		init_pair(ColorStatus, COLOR_BLACK, COLOR_WHITE);
		init_pair(ColorEdit, COLOR_WHITE, COLOR_RED);
	}

	signal(SIGINT, sighandler);
	signal(SIGTERM, sighandler);
	atexit(cleanup);
}

int
main(int argc, char *argv[])
{
	int i;

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-v")) {
			puts("sheets-"VERSION);
			exit(0);
		} else if (argv[i][0] == '-') {
			usage();
		} else {
			snprintf(filename, sizeof(filename), "%s", argv[i]);
		}
	}

	initcells();

	if (filename[0])
		readcsv(filename);

	recalc();
	initui();
	run();

	return 0;
}
