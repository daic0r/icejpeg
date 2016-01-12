//
//  common.h
//  icejpeg
//
//  Created by Matthias Gruen on 07/01/16.
//  Copyright © 2016 Matthias Gruen. All rights reserved.
//

#ifndef common_h
#define common_h

#define CF(x) clip((((x) + 64) >> 7))

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
#define ERR_OUT_OF_MEMORY					-11

typedef unsigned char byte;
typedef unsigned short word;

struct jpeg_component
{
    // we can store this globally since we only support baseline
    // jpegs with only one scan. otherwise this must be stored
    // per scan.
    byte id_dht;
    int width, height;
    int stride;
    int sx, sy;
    byte qt_table;
    byte *pixels;
    int prev_dc;
};

struct jpeg_encode_component
{
	byte id_dht;
	int width, height;
	int stride;
	int sx, sy;
	byte qt_table;
	int *pixels;
	int prev_dc;
};


byte clip(int x);
int round(float x);

#endif /* common_h */
