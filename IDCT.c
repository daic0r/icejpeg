//  *************************************************************************************
//
//  IDCT.c
//
//  version 1.0
//  01/23/2016
//  Written by Matthias Grün
//  m.gruen@theicingonthecode.com
//
//  IceJPEG is open source and may be used freely, as long as the original author
//  of the code is mentioned.
//
//  You may redistribute it freely as long as no fees are charged and this information
//  is included.
//
//  If modifications are made to the code that alter its behavior and the modified code
//  is made available to others or used in other products, the author is to receive
//  a copy of the modified code.
//
//  This code is provided as is and I do not and cannot guarantee the absence of bugs.
//  Use of this code is at your own risk and I cannot be held liable for any
//  damage that is caused by its use.
//
//  *************************************************************************************
//
//  This file constitutes the IDCT (Inverse Discrete Cosine transform) part of my IceJPEG
//  library, which was written mainly because I wanted to understand the inner workings
//  of the JPEG format.
//
//  This code is a modification of the file idct.c from libmpeg2.
//
//  Modifications include doing the upshifting of the returned value by 128 here
//  as well before returning.
//
//  *************************************************************************************

/**********************************************************/
/* inverse two dimensional DCT, Chen-Wang algorithm       */
/* (cf. IEEE ASSP-32, pp. 803-816, Aug. 1984)             */
/* 32-bit integer arithmetic (8 bit coefficients)         */
/* 11 mults, 29 adds per DCT                              */
/*                                      sE, 18.8.91       */
/**********************************************************/
/* coefficients extended to 12 bit for IEEE1180-1990      */
/* compliance                           sE,  2.1.94       */
/**********************************************************/

/* this code assumes >> to be a two's-complement arithmetic */
/* right shift: (-2)>>1 == -1 , (-3)>>1 == -2               */

#include "IDCT.h"

#define W1 5681 /* 4096*sqrt(2)*cos(1*pi/16) */
#define W2 5352 /* 4096*sqrt(2)*cos(2*pi/16) */
#define W3 4816 /* 4096*sqrt(2)*cos(3*pi/16) */
#define W5 3218 /* 4096*sqrt(2)*cos(5*pi/16) */
#define W6 2217 /* 4096*sqrt(2)*cos(6*pi/16) */
#define W7 1130 /* 4096*sqrt(2)*cos(7*pi/16) */

/* private data */
//static long temp[64];     /* intermediate storage for 1D transform */
int iclip[1024]; /* clipping table */
int *iclp;

/* row (horizontal) IDCT
 *
 *           7                       pi         1
 * dst[k] = sum c[l] * src[l] * cos( -- * ( k + - ) * l )
 *          l=0                      8          2
 *
 * where: c[0]    = 256*sqrt(0.5)
 *        c[1..7] = 256
 */

void idctrow(int *src){
  int x0, x1, x2, x3, x4, x5, x6, x7, x8;

  /* shortcut */
  if (!src[0] && !src[1] && !src[2] && !src[3] &&
      !src[4] && !src[5] && !src[6] && !src[7])
  {
    src[0]=src[1]=src[2]=src[3]=src[4]=src[5]=src[6]=src[7]=0;
    return;
  }

  /* first stage */
  x0 = src[0];
  x1 = src[4];
  x2 = src[6];
  x3 = src[2];
  x4 = src[1];
  x5 = src[7];
  x6 = src[5];
  x7 = src[3];
  x8 = W7*(x4+x5);
  x4 = x8 + (W1-W7)*x4;
  x5 = x8 - (W1+W7)*x5;
  x8 = W3*(x6+x7);
  x6 = x8 - (W3-W5)*x6;
  x7 = x8 - (W3+W5)*x7;

  /* second stage */
  x8 = ((x0+x1)<<12) + 16; /* +16 for proper rounding in the fourth stage */
  x0 = ((x0-x1)<<12) + 16;
  x1 = W6*(x3+x2);
  x2 = x1 - (W2+W6)*x2;
  x3 = x1 + (W2-W6)*x3;
  x1 = x4 + x6;
  x4 -= x6;
  x6 = x5 + x7;
  x5 -= x7;

  /* third stage */
  x7 = x8 + x3;
  x8 -= x3;
  x3 = x0 + x2;
  x0 -= x2;
  x2 = (181*(x4+x5)+128)>>8;
  x4 = (181*(x4-x5)+128)>>8;

  /* fourth stage */
  src[0] = (x7+x1)>>5;
  src[1] = (x3+x2)>>5;
  src[2] = (x0+x4)>>5;
  src[3] = (x8+x6)>>5;
  src[4] = (x8-x6)>>5;
  src[5] = (x0-x4)>>5;
  src[6] = (x3-x2)>>5;
  src[7] = (x7-x1)>>5;
}

/* column (vertical) IDCT
 *
 *             7                         pi         1
 * dst[8*k] = sum c[l] * src[8*l] * cos( -- * ( k + - ) * l )
 *            l=0                        8          2
 *
 * where: c[0]    = 1/1024*sqrt(0.5)
 *        c[1..7] = 1/1024
 */
void idctcol(int *src, unsigned char *out, int stride) {
  int x0, x1, x2, x3, x4, x5, x6, x7, x8;

  /* first stage */
  x0 = src[8*0];
  x1 = src[8*4];
  x2 = src[8*6];
  x3 = src[8*2];
  x4 = src[8*1];
  x5 = src[8*7];
  x6 = src[8*5];
  x7 = src[8*3];
  x8 = W7*(x4+x5) + 2048;
  x4 = (x8+(W1-W7)*x4)>>12;
  x5 = (x8-(W1+W7)*x5)>>12;
  x8 = W3*(x6+x7) + 2048;
  x6 = (x8-(W3-W5)*x6)>>12;
  x7 = (x8-(W3+W5)*x7)>>12;

  /* second stage */
  x8 = x0 + x1;
  x0 -= x1;
  x1 = W6*(x3+x2) + 2048;
  x2 = (x1-(W2+W6)*x2)>>12;
  x3 = (x1+(W2-W6)*x3)>>12;
  x1 = x4 + x6;
  x4 -= x6;
  x6 = x5 + x7;
  x5 -= x7;

  /* third stage */
  x7 = x8 + x3 + 512;
  x8 += -x3 + 512;
  x3 = x0 + x2 + 512;
  x0 += -x2 + 512;
  x2 = (181*(x4+x5)+128)>>8;
  x4 = (181*(x4-x5)+128)>>8;

  /* fourth stage */
  *out = iclp[(x7+x1)>>10]; out += stride;
  *out = iclp[(x3+x2)>>10]; out += stride;
  *out = iclp[(x0+x4)>>10]; out += stride;
  *out = iclp[(x8+x6)>>10]; out += stride;
  *out = iclp[(x8-x6)>>10]; out += stride;
  *out = iclp[(x0-x4)>>10]; out += stride;
  *out = iclp[(x3-x2)>>10]; out += stride;
  *out = iclp[(x7-x1)>>10];
}

void init_idct()
{
  int i;

  iclp = iclip+512;
  for (i= -512; i<512; i++)
    // daic0r: we do the upshifting her as well!
    iclp[i] = (i<-128) ? 0 : ((i>127) ? 255 : i + 128);
}
