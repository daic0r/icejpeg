#ifndef _IDCT_H
#define _IDCT_H

void idctrow(int *src);
void idctcol(int *src, unsigned char *dst, int stride);
void init_idct();

#endif