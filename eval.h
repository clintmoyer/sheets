/* See LICENSE file for copyright and license details. */

/* callback to get a cell's numeric value by address string like "A1" */
typedef double (*CellValFn)(const char *addr, int *ok);

void eval_setcellfn(CellValFn fn);
double eval_expr(const char *expr);
