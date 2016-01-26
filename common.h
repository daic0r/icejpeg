//  *************************************************************************************
//
//  common.h
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

#ifndef common_h
#define common_h

#define DESCALE8(x) CLIPBYTE((((x) + 64) >> 7))
#define max(a,b) (((a) > (b)) ? (a) : (b))
#define min(a,b) (((a) < (b)) ? (a) : (b))
#define CLIPBYTE(x) ((x) < 0 ? 0 : ((x) > 255 ? 255 : (x)))
#define CLAMPQNT(x) ((x) < 1 ? 1 : ((x) > 255 ? 255 : (x)))
#define FLIP(x) ((((x) & 0xFF) << 8) | (((x) & 0xFF00) >> 8))
#define UPR4(x) (((x) & 0xF0) >> 4)
#define LWR4(x) ((x) & 0xF)

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
#define ERR_NOT_BASELINE					-10
#define ERR_OUT_OF_MEMORY					-11
#define ERR_SCAN_BUFFER_OVERFLOW			-12
#define ERR_INVALID_LENGTH					-13
#define ERR_CANNOT_OPEN_OUTPUT_FILE         -14
#define ERR_RLC_BUFFER_OVERFLOW             -15
#define ERR_NO_HUFFMAN_CODE_FOR_SYMBOL		-16
#define ERR_INVALID_SAMPLING_FACTOR         -17

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

extern const byte jpeg_zzleft[];
extern const byte jpeg_zzright[];

int rnd(float x);

#endif /* common_h */
