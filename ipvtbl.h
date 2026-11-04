/* ipvtbl.h - shared UTF-8 box-table rendering for IPVarch */
#ifndef IPVTBL_H
#define IPVTBL_H

/* Display width of a UTF-8 string (continuation bytes occupy no column). */
int dwidth(const char *s);

/* title is centred and spans all rows. Every row has ncols cells. */
void print_table(const char *title, const char *rows[][2], int nrows);
void print_table3(const char *title, const char *rows[][3], int nrows);

#endif
