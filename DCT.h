//
//  DCT.h
//  icejpeg
//
//  Created by Matthias Gruen on 11/01/16.
//  Copyright © 2016 Matthias Gruen. All rights reserved.
//

#ifndef DCT_h
#define DCT_h

#define INT32 int
#define DCTELEM INT32

#define RIGHT_SHIFT_IS_UNSIGNED

/* We assume that right shift corresponds to signed division by 2 with
* rounding towards minus infinity.  This is correct for typical "arithmetic
* shift" instructions that shift in copies of the sign bit.  But some
* C compilers implement >> with an unsigned shift.  For these machines you
* must define RIGHT_SHIFT_IS_UNSIGNED.
* RIGHT_SHIFT provides a proper signed right shift of an INT32 quantity.
* It is only applied with constant shift counts.  SHIFT_TEMPS must be
* included in the variables of any routine using RIGHT_SHIFT.
*/

#ifdef RIGHT_SHIFT_IS_UNSIGNED
#define SHIFT_TEMPS	INT32 shift_temp;
#define RIGHT_SHIFT(x,shft)  \
((shift_temp = (x)) < 0 ? \
(shift_temp >> (shft)) | ((~((INT32) 0)) << (32-(shft))) : \
(shift_temp >> (shft)))
#else
#define SHIFT_TEMPS
#define RIGHT_SHIFT(x,shft)	((x) >> (shft))
#endif

void fdct(DCTELEM * data);

#endif /* DCT_h */
