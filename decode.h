//
//  decode.h
//  icejpeg
//
//  Created by Matthias Gruen on 11/01/16.
//  Copyright © 2016 Matthias Gruen. All rights reserved.
//

#ifndef decode_h
#define decode_h

#define ERR_OK								0
#define ERR_OPENFILE_FAILED					-1
#define ERR_NO_JPEG							-2
#define ERR_INVALID_MAJOR_REV				-3
#define ERR_INVALID_JFIF_STRING				-4
#define ERR_16BIT_DQT_NOT_SUPPORTED			-5
#define ERR_INVALID_NUMBER_OF_COMP			-6
#define ERR_INVALID_SEGMENT_SIZE			-7
#define ERR_INVALID_RST_MARKER              -8
#define ERR_SOF0_MISSING                    -9
#define ERR_PROGRESSIVE						-10

int icejpeg_init(const char* filename);
int icejpeg_read(unsigned char **buffer, int *width, int *height, int *num_components);
void icejpeg_cleanup(void);


#endif /* decode_h */
