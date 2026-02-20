/* See LICENSE file for copyright and license details. */
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "eval.h"

/*
 * Recursive descent expression evaluator.
 *
 * Grammar:
 *   expr   = term (('+' | '-') term)*
 *   term   = unary (('*' | '/') unary)*
 *   unary  = '-' unary | atom
 *   atom   = number | cellref | func '(' args ')' | '(' expr ')'
 *   func   = "SUM" | "AVG" | "MIN" | "MAX"
 *   args   = cellref ':' cellref
 */

static CellValFn cellfn;
static const char *pos;

static double parse_expr(void);

void
eval_setcellfn(CellValFn fn)
{
	cellfn = fn;
}

static void
skipws(void)
{
	while (*pos == ' ' || *pos == '\t')
		pos++;
}

/* parse cell reference, return 1 if valid */
static int
parse_cellref(const char *s, const char **end, int *col, int *row)
{
	int c = 0, r = 0;

	if (!isupper((unsigned char)*s))
		return 0;
	while (isupper((unsigned char)*s)) {
		c = c * 26 + (*s - 'A' + 1);
		s++;
	}
	if (!isdigit((unsigned char)*s))
		return 0;
	while (isdigit((unsigned char)*s)) {
		r = r * 10 + (*s - '0');
		s++;
	}
	*col = c - 1;
	*row = r - 1;
	if (end)
		*end = s;
	return 1;
}

/* get cell value via callback; addr like "A1" */
static double
getcellval(int col, int row)
{
	char addr[16];
	int ok = 0;
	double v;

	snprintf(addr, sizeof(addr), "%c%d", 'A' + col, row + 1);
	if (!cellfn)
		return 0;
	v = cellfn(addr, &ok);
	return ok ? v : 0;
}

/* evaluate range function: SUM, AVG, MIN, MAX over col1,row1:col2,row2 */
static double
eval_range(const char *func, int c1, int r1, int c2, int r2)
{
	double result = 0, v;
	int r, c, count = 0;
	int ismin, ismax;

	ismin = (strcmp(func, "MIN") == 0);
	ismax = (strcmp(func, "MAX") == 0);

	if (ismin)
		result = HUGE_VAL;
	if (ismax)
		result = -HUGE_VAL;

	for (r = r1; r <= r2; r++) {
		for (c = c1; c <= c2; c++) {
			v = getcellval(c, r);
			if (ismin && v < result)
				result = v;
			else if (ismax && v > result)
				result = v;
			else if (!ismin && !ismax)
				result += v;
			count++;
		}
	}

	if (strcmp(func, "AVG") == 0 && count > 0)
		result /= count;

	return result;
}

static double
parse_atom(void)
{
	double v;
	char *end;
	int col, row;
	char func[8];

	skipws();

	/* parenthesized expression */
	if (*pos == '(') {
		pos++;
		v = parse_expr();
		skipws();
		if (*pos == ')')
			pos++;
		return v;
	}

	/* function call: SUM(A1:B5) etc */
	if (isupper((unsigned char)*pos) && isupper((unsigned char)*(pos+1))
	    && isupper((unsigned char)*(pos+2))) {
		const char *start = pos;
		int i = 0;
		int c1, r1, c2, r2;

		while (isupper((unsigned char)*pos) && i < 7)
			func[i++] = *pos++;
		func[i] = '\0';

		skipws();
		if (*pos == '(') {
			pos++;
			skipws();
			if (parse_cellref(pos, &pos, &c1, &r1)) {
				skipws();
				if (*pos == ':') {
					pos++;
					skipws();
					if (parse_cellref(pos, &pos, &c2, &r2)) {
						skipws();
						if (*pos == ')')
							pos++;
						return eval_range(func, c1, r1, c2, r2);
					}
				}
			}
			/* fallback: try single cell in function */
			if (*pos == ')')
				pos++;
			return 0;
		}
		/* not a function, rewind and try as cell ref */
		pos = start;
	}

	/* cell reference: A1, B12, etc */
	if (parse_cellref(pos, &pos, &col, &row))
		return getcellval(col, row);

	/* number */
	v = strtod(pos, &end);
	if (end != pos) {
		pos = end;
		return v;
	}

	/* unknown token, skip */
	if (*pos)
		pos++;
	return 0;
}

static double
parse_unary(void)
{
	skipws();
	if (*pos == '-') {
		pos++;
		return -parse_unary();
	}
	return parse_atom();
}

static double
parse_term(void)
{
	double v = parse_unary();

	for (;;) {
		skipws();
		if (*pos == '*') {
			pos++;
			v *= parse_unary();
		} else if (*pos == '/') {
			double d;
			pos++;
			d = parse_unary();
			v = (d != 0) ? v / d : 0;
		} else {
			break;
		}
	}
	return v;
}

static double
parse_expr(void)
{
	double v = parse_term();

	for (;;) {
		skipws();
		if (*pos == '+') {
			pos++;
			v += parse_term();
		} else if (*pos == '-') {
			pos++;
			v -= parse_term();
		} else {
			break;
		}
	}
	return v;
}

double
eval_expr(const char *expr)
{
	pos = expr;
	return parse_expr();
}
