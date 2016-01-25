//  *************************************************************************************
//
//  encode.h
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

#ifndef _encode_h
#define _encode_h

// Bit precision for integer calculations
#define PRECISION 8

struct jpeg_encoder_settings {
	int width, height;
	int num_components;
	unsigned char quality;
	int use_rst_markers;
	struct __factors {
		int sx, sy;
	} sampling_factors[3];
};

struct jpeg_encoder_stats
{
    float bits_per_pixel;
    float compression_ratio;
    int scan_segment_size;
    struct
    {
        int min_val, max_val;
    } color_extrema[3];
};

int icejpeg_encode_init(char *filename, unsigned char *image, struct jpeg_encoder_settings *settings);
void icejpeg_setquality(unsigned char quality);
void icejpeg_set_restart_markers(int userst);
void icejpeg_get_stats(struct jpeg_encoder_stats** stats);
int icejpeg_write(void);
void icejpeg_encode_cleanup();

#endif