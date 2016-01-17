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
#define max(a,b) (((a) > (b)) ? (a) : (b))
#define min(a,b) (((a) < (b)) ? (a) : (b))

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
#define ERR_SCAN_BUFFER_OVERFLOW			-12
#define ERR_INVALID_LENGTH					-13
#define ERR_CANNOT_OPEN_OUTPUT_FILE         -14
#define ERR_RLC_BUFFER_OVERFLOW             -15

#define MAX_DC_TABLES 4
#define MAX_AC_TABLES 4

typedef unsigned char byte;
typedef unsigned short word;

#pragma pack(push)
#pragma pack(1)

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

struct jpeg_huffman_code
{
    word code;
    byte length;
};

struct jpeg_dht
{
    byte num_codes[16];
    byte *codes;
};

struct jpeg_app0
{
    char strjfif[5];
    byte maj_revision, min_revision;
    byte xy_dens_unit;
    word xdensity, ydensity;
    byte thumb_width, thumb_height;
};

// as read from the file
struct jpeg_sof0_component_info
{
    // as found in the jpeg file (3 bytes)
    byte id;
    byte sampling_factors;  //bit 0-3: vertical, 4-7: horizontal
    byte qt_table;
};

struct jpeg_sof0
{
    byte precision;
    word height, width;
    byte num_components;
};

#pragma pack(pop)

byte clip(int x);
int rnd(float x);
word flip_byte_order(word inword);

#endif /* common_h */
