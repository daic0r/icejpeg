//  *************************************************************************************
//
//  DCT.h
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
