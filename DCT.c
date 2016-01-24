//  *************************************************************************************
//
//  DCT.c
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
//  This file constitutes the DCT part of my IceJPEG library, which was
//  written mainly because I wanted to understand the inner workings of the
//  JPEG format.
//
//  This file is a modification of the code found in jfdctint.c of IJG's
//  jpeglib.
//
//  Modifications include the simplification of symbol definitions and,
//  most importantly, already removes the factor of 8 before returning,
//  so the block of data returned by this function is ready to use without further
//  processing.
//
//  *************************************************************************************

#include "DCT.h"


/*
 * The poop on this scaling stuff is as follows:
 *
 * Each 1-D DCT step produces outputs which are a factor of sqrt(N)
 * larger than the true DCT outputs.  The final outputs are therefore
 * a factor of N larger than desired; since N=8 this can be cured by
 * a simple right shift at the end of the algorithm.  The advantage of
 * this arrangement is that we save two multiplications per 1-D DCT,
 * because the y0 and y4 outputs need not be divided by sqrt(N).
 * In the IJG code, this factor of 8 is removed by the quantization step
 * (in jcdctmgr.c), NOT in this module.
 *
 * daic0r: WE REMOVE THE FACTOR OF 8 RIGHT HERE BY RIGHT-SHIFTING
 *         THE FINAL RESULTS BY 3!!
 */


#define CONST_BITS  13
#define PASS1_BITS  2


#define CONST_SCALE (1 << CONST_BITS)

/* Descale and correctly round an INT32 value that's scaled by N bits.
 * We assume RIGHT_SHIFT rounds towards minus infinity, so adding
 * the fudge factor is correct for either sign of X.
 */

#define DESCALE(x,n)  RIGHT_SHIFT((x) + (1 << ((n)-1)), n)


#define FIX_0_298631336  ((INT32)  2446)	/* FIX(0.298631336) */
#define FIX_0_390180644  ((INT32)  3196)	/* FIX(0.390180644) */
#define FIX_0_541196100  ((INT32)  4433)	/* FIX(0.541196100) */
#define FIX_0_765366865  ((INT32)  6270)	/* FIX(0.765366865) */
#define FIX_0_899976223  ((INT32)  7373)	/* FIX(0.899976223) */
#define FIX_1_175875602  ((INT32)  9633)	/* FIX(1.175875602) */
#define FIX_1_501321110  ((INT32)  12299)	/* FIX(1.501321110) */
#define FIX_1_847759065  ((INT32)  15137)	/* FIX(1.847759065) */
#define FIX_1_961570560  ((INT32)  16069)	/* FIX(1.961570560) */
#define FIX_2_053119869  ((INT32)  16819)	/* FIX(2.053119869) */
#define FIX_2_562915447  ((INT32)  20995)	/* FIX(2.562915447) */
#define FIX_3_072711026  ((INT32)  25172)	/* FIX(3.072711026) */

#define MULTIPLY(var,const)  ((var) * (const))

/*
 * Perform the forward DCT on one block of samples.
 */

void fdct(DCTELEM * data)
{
    INT32 tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7;
    INT32 tmp10, tmp11, tmp12, tmp13;
    INT32 z1, z2, z3, z4, z5;
    DCTELEM *dataptr;
    int ctr;
    SHIFT_TEMPS
    
    /* Pass 1: process rows. */
    /* Note results are scaled up by sqrt(8) compared to a true DCT; */
    /* furthermore, we scale the results by 2**PASS1_BITS. */
    
    dataptr = data;
    for (ctr = 8-1; ctr >= 0; ctr--) {
        tmp0 = dataptr[0] + dataptr[7];
        tmp7 = dataptr[0] - dataptr[7];
        tmp1 = dataptr[1] + dataptr[6];
        tmp6 = dataptr[1] - dataptr[6];
        tmp2 = dataptr[2] + dataptr[5];
        tmp5 = dataptr[2] - dataptr[5];
        tmp3 = dataptr[3] + dataptr[4];
        tmp4 = dataptr[3] - dataptr[4];
        
        /* Even part per LL&M figure 1 --- note that published figure is faulty;
         * rotator "sqrt(2)*c1" should be "sqrt(2)*c6".
         */
        
        tmp10 = tmp0 + tmp3;
        tmp13 = tmp0 - tmp3;
        tmp11 = tmp1 + tmp2;
        tmp12 = tmp1 - tmp2;
        
        dataptr[0] = (DCTELEM) ((tmp10 + tmp11) << PASS1_BITS);
        dataptr[4] = (DCTELEM) ((tmp10 - tmp11) << PASS1_BITS);
        
        z1 = MULTIPLY(tmp12 + tmp13, FIX_0_541196100);
        dataptr[2] = (DCTELEM) DESCALE(z1 + MULTIPLY(tmp13, FIX_0_765366865),
                                       CONST_BITS-PASS1_BITS);
        dataptr[6] = (DCTELEM) DESCALE(z1 + MULTIPLY(tmp12, - FIX_1_847759065),
                                       CONST_BITS-PASS1_BITS);
        
        /* Odd part per figure 8 --- note paper omits factor of sqrt(2).
         * cK represents cos(K*pi/16).
         * i0..i3 in the paper are tmp4..tmp7 here.
         */
        
        z1 = tmp4 + tmp7;
        z2 = tmp5 + tmp6;
        z3 = tmp4 + tmp6;
        z4 = tmp5 + tmp7;
        z5 = MULTIPLY(z3 + z4, FIX_1_175875602); /* sqrt(2) * c3 */
        
        tmp4 = MULTIPLY(tmp4, FIX_0_298631336); /* sqrt(2) * (-c1+c3+c5-c7) */
        tmp5 = MULTIPLY(tmp5, FIX_2_053119869); /* sqrt(2) * ( c1+c3-c5+c7) */
        tmp6 = MULTIPLY(tmp6, FIX_3_072711026); /* sqrt(2) * ( c1+c3+c5-c7) */
        tmp7 = MULTIPLY(tmp7, FIX_1_501321110); /* sqrt(2) * ( c1+c3-c5-c7) */
        z1 = MULTIPLY(z1, - FIX_0_899976223); /* sqrt(2) * (c7-c3) */
        z2 = MULTIPLY(z2, - FIX_2_562915447); /* sqrt(2) * (-c1-c3) */
        z3 = MULTIPLY(z3, - FIX_1_961570560); /* sqrt(2) * (-c3-c5) */
        z4 = MULTIPLY(z4, - FIX_0_390180644); /* sqrt(2) * (c5-c3) */
        
        z3 += z5;
        z4 += z5;
        
        dataptr[7] = (DCTELEM) DESCALE(tmp4 + z1 + z3, CONST_BITS-PASS1_BITS);
        dataptr[5] = (DCTELEM) DESCALE(tmp5 + z2 + z4, CONST_BITS-PASS1_BITS);
        dataptr[3] = (DCTELEM) DESCALE(tmp6 + z2 + z3, CONST_BITS-PASS1_BITS);
        dataptr[1] = (DCTELEM) DESCALE(tmp7 + z1 + z4, CONST_BITS-PASS1_BITS);
        
        dataptr += 8;		/* advance pointer to next row */
    }
    
    /* Pass 2: process columns.
     * We remove the PASS1_BITS scaling, but leave the results scaled up
     * by an overall factor of 8.
     */
    
    dataptr = data;
    for (ctr = 8-1; ctr >= 0; ctr--) {
        tmp0 = dataptr[8*0] + dataptr[8*7];
        tmp7 = dataptr[8*0] - dataptr[8*7];
        tmp1 = dataptr[8*1] + dataptr[8*6];
        tmp6 = dataptr[8*1] - dataptr[8*6];
        tmp2 = dataptr[8*2] + dataptr[8*5];
        tmp5 = dataptr[8*2] - dataptr[8*5];
        tmp3 = dataptr[8*3] + dataptr[8*4];
        tmp4 = dataptr[8*3] - dataptr[8*4];
        
        /* Even part per LL&M figure 1 --- note that published figure is faulty;
         * rotator "sqrt(2)*c1" should be "sqrt(2)*c6".
         */
        
        tmp10 = tmp0 + tmp3;
        tmp13 = tmp0 - tmp3;
        tmp11 = tmp1 + tmp2;
        tmp12 = tmp1 - tmp2;
        
        dataptr[8*0] = (DCTELEM) DESCALE(tmp10 + tmp11, PASS1_BITS) >> 3;
        dataptr[8*4] = (DCTELEM) DESCALE(tmp10 - tmp11, PASS1_BITS) >> 3;
        
        z1 = MULTIPLY(tmp12 + tmp13, FIX_0_541196100);
        dataptr[8*2] = (DCTELEM) DESCALE(z1 + MULTIPLY(tmp13, FIX_0_765366865),
                                               CONST_BITS+PASS1_BITS) >> 3;
        dataptr[8*6] = (DCTELEM) DESCALE(z1 + MULTIPLY(tmp12, - FIX_1_847759065),
                                               CONST_BITS+PASS1_BITS) >> 3;
        
        /* Odd part per figure 8 --- note paper omits factor of sqrt(2).
         * cK represents cos(K*pi/16).
         * i0..i3 in the paper are tmp4..tmp7 here.
         */
        
        z1 = tmp4 + tmp7;
        z2 = tmp5 + tmp6;
        z3 = tmp4 + tmp6;
        z4 = tmp5 + tmp7;
        z5 = MULTIPLY(z3 + z4, FIX_1_175875602); /* sqrt(2) * c3 */
        
        tmp4 = MULTIPLY(tmp4, FIX_0_298631336); /* sqrt(2) * (-c1+c3+c5-c7) */
        tmp5 = MULTIPLY(tmp5, FIX_2_053119869); /* sqrt(2) * ( c1+c3-c5+c7) */
        tmp6 = MULTIPLY(tmp6, FIX_3_072711026); /* sqrt(2) * ( c1+c3+c5-c7) */
        tmp7 = MULTIPLY(tmp7, FIX_1_501321110); /* sqrt(2) * ( c1+c3-c5-c7) */
        z1 = MULTIPLY(z1, - FIX_0_899976223); /* sqrt(2) * (c7-c3) */
        z2 = MULTIPLY(z2, - FIX_2_562915447); /* sqrt(2) * (-c1-c3) */
        z3 = MULTIPLY(z3, - FIX_1_961570560); /* sqrt(2) * (-c3-c5) */
        z4 = MULTIPLY(z4, - FIX_0_390180644); /* sqrt(2) * (c5-c3) */
        
        z3 += z5;
        z4 += z5;
        
        dataptr[8*7] = (DCTELEM) DESCALE(tmp4 + z1 + z3,
                                               CONST_BITS+PASS1_BITS) >> 3;
        dataptr[8*5] = (DCTELEM) DESCALE(tmp5 + z2 + z4,
                                               CONST_BITS+PASS1_BITS) >> 3;
        dataptr[8*3] = (DCTELEM) DESCALE(tmp6 + z2 + z3,
                                               CONST_BITS+PASS1_BITS) >> 3;
        dataptr[8*1] = (DCTELEM) DESCALE(tmp7 + z1 + z4,
                                               CONST_BITS+PASS1_BITS) >> 3;
        
        dataptr++;			/* advance pointer to next column */
    }
}