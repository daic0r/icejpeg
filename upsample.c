//  *************************************************************************************
//
//  upsample.c
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
//  This file constitutes the upsampling code of my decoder.
//
//  The bicubic upsampling code is basically identical to the one used in Martin Fiedler's
//  NanoJPEG, which I consulted to understand the upsampling algorithm. Thanks to Martin at
//  this point for answering my questions on his blog.
//
//  The Lanczos upsampling was written by me but follows the same pattern.
//
//  *************************************************************************************

#include "upsample.h"
#include <stdio.h>
#include <stdlib.h>
#include <memory.h>

#define CF4A (-9) // 5
#define CF4B (111) // 1
#define CF4C (29) // 3
#define CF4D (-3) // 7

#define CF3A (28)
#define CF3B (109)
#define CF3C (-9)

#define CF3X (104)
#define CF3Y (27)
#define CF3Z (-3)

#define CF2A (139)
#define CF2B (-11)


// Lanczos coefficients
#define LC4_5 -11
#define LC4_1 112
#define LC4_3 30
#define LC4_7 -2

#define LC3A_3 29
#define LC3A_1 109
#define LC3A_5 -10

#define LC3B_1 102
#define LC3B_3 27
#define LC3B_7 -1

#define LC2_1 141
#define LC2_5 -13


// Lanczos-3 coefficients
#define LC6_9 4
#define LC6_5 -17
#define LC6_1 114
#define LC6_3 35
#define LC6_7 -9
#define LC6_11 1

#define LC5A_7 -9
#define LC5A_3 35
#define LC5A_1 115
#define LC5A_5 -17
#define LC5A_9 4

#define LC5B_5 -18
#define LC5B_1 118
#define LC5B_3 36
#define LC5B_7 -9
#define LC5B_11 1

#define LC4A_3 33
#define LC4A_1 107
#define LC4A_5 -16
#define LC4A_9 4

#define LC4B_1 103
#define LC4B_3 32
#define LC4B_7 -8
#define LC4B_11 1

#define LC3_1 144
#define LC3_5 -21
#define LC3_9 5


/****************************************************/
/* Bicubic upsampling                               */
/****************************************************/
// doubles the width, preserves the height
void upsampleBicubicH(struct jpeg_component *c)
{
	byte* outBuf = (byte*)malloc(((c->width * c->height) << 1) * sizeof(byte));
	memset(outBuf, 0, ((c->width * c->height) << 1) * sizeof(byte));

	byte *curPos = outBuf;
    byte *inBuf = c->pixels;

	int x, y;

	for (y = 0; y < c->height; y++)
	{
		// Less than 4 bicubic functions affect those first 3 entries
		curPos[0] = DESCALE8(inBuf[0] * CF2A + inBuf[1] * CF2B);
		curPos[1] = DESCALE8(inBuf[0] * CF3X + inBuf[1] * CF3Y + inBuf[2] * CF3Z);
		curPos[2] = DESCALE8(inBuf[0] * CF3A + inBuf[1] * CF3B + inBuf[2] * CF3C);
		for (x = 0; x < c->width - 3; x++)
		{
			curPos[(x << 1) + 3] = DESCALE8(inBuf[x] * CF4A + inBuf[x + 1] * CF4B + inBuf[x + 2] * CF4C + inBuf[x + 3] * CF4D);
			curPos[(x << 1) + 4] = DESCALE8(inBuf[x] * CF4D + inBuf[x + 1] * CF4C + inBuf[x + 2] * CF4B + inBuf[x + 3] * CF4A);
		}
		// go to beginning of next line
		curPos += c->width << 1;
		inBuf += c->stride;
		// and fill the last 3 entries in the previous line
		curPos[-3] = DESCALE8(inBuf[-3] * CF3C + inBuf[-2] * CF3B + inBuf[-1] * CF3A);
		curPos[-2] = DESCALE8(inBuf[-3] * CF3Z + inBuf[-2] * CF3Y + inBuf[-1] * CF3X);
		curPos[-1] = DESCALE8(inBuf[-2] * CF2B + inBuf[-1] * CF2A);
	}
    
    c->width <<= 1;
    c->stride = c->width;
    free((void*) c->pixels);
    c->pixels = outBuf;
}

// doubles the height, leaves the width untouched
void upsampleBicubicV(struct jpeg_component *c)
{
	byte* outBuf = (byte*)malloc(((c->width * c->height) << 1) * sizeof(byte));
	memset(outBuf, 0, ((c->width * c->height) << 1) * sizeof(byte));

	byte *curPos = outBuf;
	int s1 = c->stride;
	int s2 = 2 * s1;
	int s3 = 3 * s1;

	int x, y;

	const byte *inBuf = 0L;

	for (x = 0; x < c->width; x++)
	{
		curPos = outBuf + x;
		inBuf = c->pixels + x;
		// Less than 4 bicubic functions affect those first 3 entries
		*curPos = DESCALE8(inBuf[0] * CF2A + inBuf[s1] * CF2B); curPos += c->width;
		*curPos = DESCALE8(inBuf[0] * CF3X + inBuf[s1] * CF3Y + inBuf[s2] * CF3Z); curPos += c->width;
		*curPos = DESCALE8(inBuf[0] * CF3A + inBuf[s1] * CF3B + inBuf[s2] * CF3C); curPos += c->width;
		for (y = 0; y < c->height - 3; y++)
		{
			*curPos = DESCALE8(inBuf[0] * CF4A + inBuf[s1] * CF4B + inBuf[s2] * CF4C + inBuf[s3] * CF4D); curPos += c->width;
			*curPos = DESCALE8(inBuf[0] * CF4D + inBuf[s1] * CF4C + inBuf[s2] * CF4B + inBuf[s3] * CF4A); curPos += c->width;
			inBuf += s1;
		}
		// and fill the last 3 entries
		*curPos = DESCALE8(inBuf[0] * CF3C + inBuf[s1] * CF3B + inBuf[s2] * CF3A); curPos += c->width;
		*curPos = DESCALE8(inBuf[0] * CF3Z + inBuf[s1] * CF3Y + inBuf[s2] * CF3X); curPos += c->width;
		*curPos = DESCALE8(inBuf[s1] * CF2B + inBuf[s2] * CF2A);
	}
    c->height <<= 1;
    c->stride = c->width;
    free((void*) c->pixels);
    c->pixels = outBuf;
}

/****************************************************/
/* Lanczos upsampling                               */
/****************************************************/
// doubles the width, preserves the height
void upsampleLanczosH(struct jpeg_component *c)
{
    byte* outBuf = (byte*)malloc(((c->width * c->height) << 1) * sizeof(byte));
    memset(outBuf, 0, ((c->width * c->height) << 1) * sizeof(byte));
    
    byte *curPos = outBuf;
    byte *inBuf = c->pixels;
    
    int x, y;
    
    for (y = 0; y < c->height; y++)
    {
        // Less than 4 bicubic functions affect those first 3 entries
        curPos[0] = DESCALE8(inBuf[0] * LC3_1 + inBuf[1] * LC3_5 + inBuf[2] * LC3_9);
        curPos[1] = DESCALE8(inBuf[0] * LC4B_1 + inBuf[1] * LC4B_3 + inBuf[2] * LC4B_7 + inBuf[3] * LC4B_11);
        curPos[2] = DESCALE8(inBuf[0] * LC4A_3 + inBuf[1] * LC4A_1 + inBuf[2] * LC4A_5 + inBuf[3] * LC4A_9);
        curPos[3] = DESCALE8(inBuf[0] * LC5B_5 + inBuf[1] * LC5B_1 + inBuf[2] * LC5B_3 + inBuf[3] * LC5B_7 + inBuf[4] * LC5B_11);
        curPos[4] = DESCALE8(inBuf[0] * LC5A_7 + inBuf[1] * LC5A_3 + inBuf[2] * LC5A_1 + inBuf[3] * LC5A_5 + inBuf[4] * LC5A_9);
        for (x = 0; x < c->width - 5; x++)
        {
            curPos[(x << 1) + 5] = DESCALE8(inBuf[x] * LC6_9 + inBuf[x + 1] * LC6_5 + inBuf[x + 2] * LC6_1 + inBuf[x + 3] * LC6_3 + inBuf[x + 4] * LC6_7 + inBuf[x + 5] * LC6_11);
            curPos[(x << 1) + 6] = DESCALE8(inBuf[x] * LC6_11 + inBuf[x + 1] * LC6_7 + inBuf[x + 2] * LC6_3 + inBuf[x + 3] * LC6_1  + inBuf[x + 4] * LC6_5 + inBuf[x + 5] * LC6_9);
        }
        // go to beginning of next line
        curPos += c->width << 1;
        inBuf += c->stride;
        // and fill the last 3 entries in the previous line
        curPos[-5] = DESCALE8(inBuf[-5] * LC5A_9 + inBuf[-4] * LC5A_5 + inBuf[-3] * LC5A_1 + inBuf[-2] * LC5A_3 + inBuf[-1] * LC5A_7);
        curPos[-4] = DESCALE8(inBuf[-5] * LC5B_11 + inBuf[-4] * LC5B_7 + inBuf[-3] * LC5B_3 + inBuf[-2] * LC5B_1 + inBuf[-1] * LC5B_5);
        curPos[-3] = DESCALE8(inBuf[-4] * LC4A_9 + inBuf[-3] * LC4A_5 + inBuf[-2] * LC3A_1 + inBuf[-1] * LC3A_3);
        curPos[-2] = DESCALE8(inBuf[-4] * LC4B_11 + inBuf[-3] * LC4B_7 + inBuf[-2] * LC4B_3 + inBuf[-1] * LC4B_1);
        curPos[-1] = DESCALE8(inBuf[-3] * LC3_9 + inBuf[-2] * LC3_5 + inBuf[-1] * LC3_1);
    }
    
    c->width <<= 1;
    c->stride = c->width;
    free((void*) c->pixels);
    c->pixels = outBuf;
}

// doubles the height, leaves the width untouched
void upsampleLanczosV(struct jpeg_component *c)
{
    byte* outBuf = (byte*)malloc(((c->width * c->height) << 1) * sizeof(byte));
    memset(outBuf, 0, ((c->width * c->height) << 1) * sizeof(byte));
    
    byte *curPos = outBuf;
    int s1 = c->stride;
    int s2 = 2 * s1;
    int s3 = 3 * s1;
    int s4 = 4 * s1;
    int s5 = 5 * s1;
    
    int x, y;
    
    const byte *inBuf = 0L;
    
    for (x = 0; x < c->width; x++)
    {
        curPos = outBuf + x;
        inBuf = c->pixels + x;
        // Less than 6 lanczos functions affect those first 5 entries
        *curPos = DESCALE8(inBuf[0] * LC3_1 + inBuf[s1] * LC3_5 + inBuf[s2] * LC3_9); curPos += c->width;
        *curPos = DESCALE8(inBuf[0] * LC4B_1 + inBuf[s1] * LC4B_3 + inBuf[s2] * LC4B_7 + inBuf[s3] * LC4B_11); curPos += c->width;
        *curPos = DESCALE8(inBuf[0] * LC4A_3 + inBuf[s1] * LC4A_1 + inBuf[s2] * LC4A_5 + inBuf[s3] * LC4A_9); curPos += c->width;
        *curPos = DESCALE8(inBuf[0] * LC5B_5 + inBuf[s1] * LC5B_1 + inBuf[s2] * LC5B_3 + inBuf[s3] * LC5B_7 + inBuf[s4] * LC5B_11); curPos += c->width;
        *curPos = DESCALE8(inBuf[0] * LC5A_7 + inBuf[s1] * LC5A_3 + inBuf[s2] * LC5A_1 + inBuf[s3] * LC5A_5 + inBuf[s4] * LC5A_9); curPos += c->width;
        for (y = 0; y < c->height - 5; y++)
        {
            *curPos = DESCALE8(inBuf[0] * LC6_9 + inBuf[s1] * LC6_5 + inBuf[s2] * LC6_1 + inBuf[s3] * LC6_3 + inBuf[s4] * LC6_7 + inBuf[s5] * LC6_11); curPos += c->width;
            *curPos = DESCALE8(inBuf[0] * LC6_11 + inBuf[s1] * LC6_7 + inBuf[s2] * LC6_3 + inBuf[s3] * LC6_1 + inBuf[s4] * LC6_5 + inBuf[s5] * LC6_9); curPos += c->width;
            inBuf += s1;
        }
        // and fill the last 5 entries
        *curPos = DESCALE8(inBuf[0] * LC5A_9 + inBuf[s1] * LC5A_5 + inBuf[s2] * LC5A_1 + inBuf[s3] * LC5A_3 + inBuf[s4] * LC5A_7); curPos += c->width;
        *curPos = DESCALE8(inBuf[0] * LC5B_11 + inBuf[s1] * LC5B_7 + inBuf[s2] * LC5B_3 + inBuf[s3] * LC5B_1 + inBuf[s4] * LC5B_5); curPos += c->width;
        *curPos = DESCALE8(inBuf[0] * LC4A_9 + inBuf[s1] * LC4A_5 + inBuf[s2] * LC4A_1 + inBuf[s3] * LC4A_3); curPos += c->width;
        *curPos = DESCALE8(inBuf[0] * LC4B_11 + inBuf[s1] * LC4B_7 + inBuf[s2] * LC4B_3 + inBuf[s3] * LC4B_1); curPos += c->width;
        *curPos = DESCALE8(inBuf[0] * LC3_9 + inBuf[s1] * LC3_5 + inBuf[s2] * LC3_1);
    }
    c->height <<= 1;
    c->stride = c->width;
    free((void*) c->pixels);
    c->pixels = outBuf;
}