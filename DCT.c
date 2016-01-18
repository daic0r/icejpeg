/*
 * jfdctint.c
 *
 * Copyright (C) 1991-1996, Thomas G. Lane.
 * This file is part of the Independent JPEG Group's software.
 * For conditions of distribution and use, see the accompanying README file.
 *
 * This file contains a slow-but-accurate integer implementation of the
 * forward DCT (Discrete Cosine Transform).
 *
 * A 2-D DCT can be done by 1-D DCT on each row followed by 1-D DCT
 * on each column.  Direct algorithms are also available, but they are
 * much more complex and seem not to be any faster when reduced to code.
 *
 * This implementation is based on an algorithm described in
 *   C. Loeffler, A. Ligtenberg and G. Moschytz, "Practical Fast 1-D DCT
 *   Algorithms with 11 Multiplications", Proc. Int'l. Conf. on Acoustics,
 *   Speech, and Signal Processing 1989 (ICASSP '89), pp. 988-991.
 * The primary algorithm described there uses 11 multiplies and 29 adds.
 * We use their alternate method with 12 multiplies and 32 adds.
 * The advantage of this method is that no data path contains more than one
 * multiplication; this allows a very simple and accurate implementation in
 * scaled fixed-point arithmetic, with a minimal number of shifts.
 */

#define JPEG_INTERNALS

#include "DCT.h"

//#ifdef DCT_IFAST_SUPPORTED

#define BITS_IN_JSAMPLE 8

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

#if BITS_IN_JSAMPLE == 8
#define CONST_BITS  13
#define PASS1_BITS  2
#else
#define CONST_BITS  13
#define PASS1_BITS  1		/* lose a little precision to avoid overflow */
#endif

/*
 * Macros for handling fixed-point arithmetic; these are used by many
 * but not all of the DCT/IDCT modules.
 *
 * All values are expected to be of type INT32.
 * Fractional constants are scaled left by CONST_BITS bits.
 * CONST_BITS is defined within each module using these macros,
 * and may differ from one module to the next.
 */

#define ONE	((INT32) 1)
#define CONST_SCALE (ONE << CONST_BITS)

/* Convert a positive real constant to an integer scaled by CONST_SCALE.
 * Caution: some C compilers fail to reduce "FIX(constant)" at compile time,
 * thus causing a lot of useless floating-point operations at run time.
 */

#define FIX(x)	((INT32) ((x) * CONST_SCALE + 0.5))

/* Descale and correctly round an INT32 value that's scaled by N bits.
 * We assume RIGHT_SHIFT rounds towards minus infinity, so adding
 * the fudge factor is correct for either sign of X.
 */

#define DESCALE(x,n)  RIGHT_SHIFT((x) + (ONE << ((n)-1)), n)


/* Some C compilers fail to reduce "FIX(constant)" at compile time, thus
 * causing a lot of useless floating-point operations at run time.
 * To get around this we use the following pre-calculated constants.
 * If you change CONST_BITS you may want to add appropriate values.
 * (With a reasonable C compiler, you can just rely on the FIX() macro...)
 */

#if CONST_BITS == 13
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
#else
#define FIX_0_298631336  FIX(0.298631336)
#define FIX_0_390180644  FIX(0.390180644)
#define FIX_0_541196100  FIX(0.541196100)
#define FIX_0_765366865  FIX(0.765366865)
#define FIX_0_899976223  FIX(0.899976223)
#define FIX_1_175875602  FIX(1.175875602)
#define FIX_1_501321110  FIX(1.501321110)
#define FIX_1_847759065  FIX(1.847759065)
#define FIX_1_961570560  FIX(1.961570560)
#define FIX_2_053119869  FIX(2.053119869)
#define FIX_2_562915447  FIX(2.562915447)
#define FIX_3_072711026  FIX(3.072711026)
#endif


/* Multiply an INT32 variable by an INT32 constant to yield an INT32 result.
 * For 8-bit samples with the recommended scaling, all the variable
 * and constant values involved are no more than 16 bits wide, so a
 * 16x16->32 bit multiply can be used instead of a full 32x32 multiply.
 * For 12-bit samples, a full 32-bit multiplication will be needed.
 */

#ifndef MULTIPLY16C16		/* default definition */
#define MULTIPLY16C16(var,const)  ((var) * (const))
#endif

#if BITS_IN_JSAMPLE == 8
#define MULTIPLY(var,const)  MULTIPLY16C16(var,const)
#else
#define MULTIPLY(var,const)  ((var) * (const))
#endif


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