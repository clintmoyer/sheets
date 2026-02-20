/* See LICENSE file for copyright and license details. */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"
#include "eval.h"
#include "config.h"

#define CELLTEXT 256

/* typedefs */
typedef struct {
	char text[CELLTEXT]; /* raw text / formula */
	double val;          /* computed numeric value */
	int hasval;          /* 1 if val is valid */
} Cell;

/* globals */
static Cell *cells;      /* flat array: cells[row * maxcols + col] */
static char filename[512];
static int dirty;        /* unsaved changes flag */

/* macros */
#define CELL(r, c) (&cells[(r) * maxcols + (c)])

/* forward declarations */
static int celladdr(const char *s, int *row, int *col);

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

/* get display value of a cell as string */
void
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

int
main(int argc, char *argv[])
{
	(void)argc;
	(void)argv;

	initcells();
	return 0;
}
