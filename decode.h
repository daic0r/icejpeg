//  *************************************************************************************
//
//  decode.h
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

#ifndef decode_h
#define decode_h

int icejpeg_decode_init(const char* filename);
int icejpeg_read(unsigned char **buffer, int *width, int *height, int *num_components);
void icejpeg_cleanup(void);


#endif /* decode_h */
