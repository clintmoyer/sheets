/* See LICENSE file for copyright and license details. */

/* default column width in characters */
static int colwidth = 10;

/* default number of columns */
static int maxcols = 26;

/* default number of rows */
static int maxrows = 100;

/* default separator for CSV files */
static char separator = ',';

/* colors: foreground, background pairs (ncurses color pair index) */
enum {
	ColorNorm = 1,    /* normal cells */
	ColorSel,         /* selected cell */
	ColorHeader,      /* row/column headers */
	ColorStatus,      /* status bar */
	ColorEdit,        /* edit mode */
};
