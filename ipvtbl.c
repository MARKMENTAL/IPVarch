/* ipvtbl.c - shared UTF-8 box-table rendering for IPVarch */
#include <stdio.h>
#include <string.h>
#include "ipvtbl.h"

/* UTF-8 encoded box drawing characters (U+2500 block). */
#define BOX_H  "\xe2\x94\x80"   /* horizontal line */
#define BOX_V  "\xe2\x94\x82"   /* vertical line   */
#define BOX_TL "\xe2\x94\x8c"   /* top left     ┌ */
#define BOX_TR "\xe2\x94\x90"   /* top right    ┐ */
#define BOX_BL "\xe2\x94\x94"   /* bottom left  └ */
#define BOX_BR "\xe2\x94\x98"   /* bottom right ┘ */
#define BOX_TJ "\xe2\x94\xac"   /* top join     ┬ */
#define BOX_BJ "\xe2\x94\xb4"   /* bottom join  ┴ */
#define BOX_LJ "\xe2\x94\x9c"   /* left join    ├ */
#define BOX_RJ "\xe2\x94\xa4"   /* right join   ┤ */
#define BOX_CJ "\xe2\x94\xbc"   /* centre join  ┼ */

int dwidth(const char *s)
{
    int w = 0;
    while (*s != '\0') {
        if ((((unsigned char)*s) & 0xC0) != 0x80)
            w++;
        s++;
    }
    return w;
}

static void dashes(int n)
{
    while (n-- > 0)
        fputs(BOX_H, stdout);
    return;
}

/* Emit a cell of exactly 'w' display columns: space + text + right pad. */
static void padcell(const char *s, int w)
{
    int d = dwidth(s) + 1;
    putchar(' ');
    fputs(s, stdout);
    while (d < w) {
        putchar(' ');
        d++;
    }
    return;
}

/* Centre text within 'w' display columns. */
static void centercell(const char *s, int w)
{
    int d = dwidth(s) + 1;
    int l = (w - d) / 2;
    int r = w - d - l;
    while (l-- > 0)
        putchar(' ');
    putchar(' ');
    fputs(s, stdout);
    while (r-- > 0)
        putchar(' ');
    return;
}

void print_table(const char *title, const char *rows[][2], int nrows)
{
    int w1 = 0, w2 = 0, tw, i;

    for (i = 0; i < nrows; i++) {
        int a = dwidth(rows[i][0]) + 2;   /* room for cell padding */
        int b = dwidth(rows[i][1]) + 2;
        if (a > w1) w1 = a;
        if (b > w2) w2 = b;
    }
    tw = dwidth(title) + 2;
    if (tw > w1 + w2 - 1)
        w2 = tw - w1 + 1;

    fputs(BOX_TL, stdout); dashes(w1); fputs(BOX_TJ, stdout);
    dashes(w2); fputs(BOX_TR "\n", stdout);

    fputs(BOX_V, stdout); centercell(title, w1 + 1 + w2); fputs(BOX_V "\n", stdout);

    fputs(BOX_LJ, stdout); dashes(w1); fputs(BOX_CJ, stdout);
    dashes(w2); fputs(BOX_RJ "\n", stdout);

    for (i = 0; i < nrows; i++) {
        fputs(BOX_V, stdout); padcell(rows[i][0], w1);
        fputs(BOX_V, stdout); padcell(rows[i][1], w2);
        fputs(BOX_V "\n", stdout);
    }

    fputs(BOX_BL, stdout); dashes(w1); fputs(BOX_BJ, stdout);
    dashes(w2); fputs(BOX_BR "\n", stdout);
    return;
}

void print_table3(const char *title, const char *rows[][3], int nrows)
{
    int w1 = 0, w2 = 0, w3 = 0, tw, i;

    for (i = 0; i < nrows; i++) {
        int a = dwidth(rows[i][0]) + 2;
        int b = dwidth(rows[i][1]) + 2;
        int c = dwidth(rows[i][2]) + 2;
        if (a > w1) w1 = a;
        if (b > w2) w2 = b;
        if (c > w3) w3 = c;
    }
    tw = dwidth(title) + 2;
    if (tw > w1 + w2 + w3)
        w3 = tw - w1 - w2 + 1;

    fputs(BOX_TL, stdout); dashes(w1); fputs(BOX_TJ, stdout);
    dashes(w2); fputs(BOX_TJ, stdout); dashes(w3); fputs(BOX_TR "\n", stdout);

    fputs(BOX_V, stdout); centercell(title, w1 + 1 + w2 + 1 + w3);
    fputs(BOX_V "\n", stdout);

    fputs(BOX_LJ, stdout); dashes(w1); fputs(BOX_CJ, stdout);
    dashes(w2); fputs(BOX_CJ, stdout); dashes(w3); fputs(BOX_RJ "\n", stdout);

    for (i = 0; i < nrows; i++) {
        fputs(BOX_V, stdout); padcell(rows[i][0], w1);
        fputs(BOX_V, stdout); padcell(rows[i][1], w2);
        fputs(BOX_V, stdout); padcell(rows[i][2], w3);
        fputs(BOX_V "\n", stdout);
    }

    fputs(BOX_BL, stdout); dashes(w1); fputs(BOX_BJ, stdout);
    dashes(w2); fputs(BOX_BJ, stdout); dashes(w3); fputs(BOX_BR "\n", stdout);
    return;
}
