#include "encode.h"
#include "common.h"
#include "DCT.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>

#define _JPEG_ENCODER_DEBUG

#define UPSCALE(x) ((x) << PRECISION)
#define DESCALE(x) (((x) + (PRECISION >> 1)) >> PRECISION)

#define YR 77
#define YG 150
#define YB 29

#define CBR -43
#define CBG -85
#define CBB 128

#define CRR 128
#define CRG -107
#define CRB -21

const int color_conversion_coeff[3][4] = { { YR, YG, YB, 0 }, { CBR, CBG, CBB, 128 }, { CRR, CRG, CRB, 128} };

const byte jpeg_zz[] = {
    0, 1, 5, 6, 14, 15, 27, 28,
    2, 4, 7, 13, 16, 26, 29, 42,
    3, 8, 12, 17, 25, 30, 41, 43,
    9, 11, 18, 24, 31, 40, 44, 53,
    10, 19, 23, 32, 39, 45, 52, 54,
    20, 22, 33, 38, 46, 51, 55, 60,
    21, 34, 37, 47, 50, 56, 59, 61,
    35, 36, 48, 49, 57, 58, 62, 63
};

const byte jpeg_qtbl_luminance[] = {
	16, 11, 10, 16, 124, 140, 151, 161,
	12, 12, 14, 19, 126, 158, 160, 155,
	14, 13, 16, 24, 140, 157, 169, 156,
	14, 17, 22, 29, 151, 187, 180, 162,
	18, 22, 37, 56, 168, 109, 103, 177,
	24, 35, 55, 64, 181, 104, 113, 192,
	49, 64, 78, 87, 103, 121, 120, 101,
	72, 92, 95, 98, 112, 100, 103, 199
};

const byte jpeg_qtbl_chrominance[] = {
	17, 18, 24, 47, 99, 99, 99, 99,
	18, 21, 26, 66, 99, 99, 99, 99,
	24, 26, 56, 99, 99, 99, 99, 99,
	47, 66, 99, 99, 99, 99, 99, 99,
	99, 99, 99, 99, 99, 99, 99, 99,
	99, 99, 99, 99, 99, 99, 99, 99,
	99, 99, 99, 99, 99, 99, 99, 99,
	99, 99, 99, 99, 99, 99, 99, 99
};

const byte* jpeg_qtbl_selector[] = {
	jpeg_qtbl_luminance, jpeg_qtbl_chrominance, jpeg_qtbl_chrominance
};

struct jpeg_bit_string {
	byte length;
	int bits;
};

struct jpeg_zrlc {
	byte info; // bit 7-4: number of zeros, bit 3-0: category
	struct jpeg_bit_string value;
};

struct __ice_env
{
	const char outfile[40];
	int *image;
	int width, height;
	int num_components;
	int max_sx, max_sy;
	int num_mcu_x, num_mcu_y;
	int mcu_width, mcu_height;
    int cur_mcu_x, cur_mcu_y;
    int block[64];
	int dcpred;
} iceenv;


struct jpeg_encode_component
{
	byte id_dht;
	int width, height;
	int stride;
	int sx, sy;
	byte qt_table;
	int *pixels;
	int prev_dc;
	struct jpeg_zrlc *rlc;
	int rlc_index;
};

struct jpeg_encode_component icecomp[3];

static void print_block(int block[64])
{
    int x, y;
    for (y = 0; y < 8; y++)
    {
        for (x = 0; x < 8; x++)
        {
            printf("%d ", block[(y * 8) + x]);
        }
        printf("\n");
    }
    
    printf("\n");
}

static void generate_constants()
{
	int y_r = rnd(0.299f * (1 << PRECISION));
	int y_g = rnd(0.587f * (1 << PRECISION));
	int y_b = rnd(0.114f * (1 << PRECISION));

	int cb_r = rnd(-0.1687f * (1 << PRECISION));
	int cb_g = rnd(-0.3313f * (1 << PRECISION));
	int cb_b = rnd(0.5f * (1 << PRECISION));

	int cr_r = rnd(0.5f * (1 << PRECISION));
	int cr_g = rnd(-0.4187f * (1 << PRECISION));
	int cr_b = rnd(-0.0813f * (1 << PRECISION));

	printf("%d\n", y_r);
	printf("%d\n", y_g);
	printf("%d\n\n", y_b);

	printf("%d\n", cb_r);
	printf("%d\n", cb_g);
	printf("%d\n\n", cb_b);

	printf("%d\n", cr_r);
	printf("%d\n", cr_g);
	printf("%d\n", cr_b);

}

// Find the number of bits necessary to represent an int value
inline byte find_category(int num)
{
	byte category = 0;
	while ((num < ((-1 << category) + 1)) || (num > (1 << category) - 1))
		category++;

	return category;
}

// Get the bit coding of an int value
inline int get_bit_coding(int num, byte category)
{
	register int code = num & ((1 << category) - 1);
	if (num < 0)
		code--;
	return code;
}

static void downsample()
{
	int i = 0;
	int *tmpimage = 0;
	int *outpixels = 0;
	for (i = 0; i < iceenv.num_components; i++)
	{
		icecomp[i].pixels = (int*)malloc(icecomp[i].stride * iceenv.height * sizeof(int));

		int x, y;
		// Go to correct start component
		tmpimage = iceenv.image + i;

		outpixels = icecomp[i].pixels;

		// Do the rows first
		for (y = 0; y < iceenv.height; y++)
		{
			//outpixels = icecomp[i].pixels + (y * icecomp[i].stride);
			int* start_index = outpixels;
			int step_x = iceenv.max_sx / icecomp[i].sx;
			for (x = 0; x < iceenv.width; x += step_x)
			{
				register int pixel_avg = 0;
				int x2;
				for (x2 = 0; x2 < step_x; x2++)
				{
                    pixel_avg += *tmpimage;
                    tmpimage += iceenv.num_components;
				}
				pixel_avg /= step_x;
				*outpixels++ = pixel_avg;
			}
			int last_val = *(outpixels - 1);
			// fill rest of the buffer with value of rightmost pixel
			while (outpixels < start_index + icecomp[i].stride)
            {
				*outpixels++ = last_val;
            }
		}

		int *srcimage2 = icecomp[i].pixels;
        int new_height = ((icecomp[i].sy << 3) * iceenv.num_mcu_y);
		icecomp[i].pixels = (int*)malloc(icecomp[i].stride * new_height * sizeof(int));

		outpixels = icecomp[i].pixels;
		int *cur_srcimage = 0;

#ifdef _JPEG_ENCODER_DEBUG
        int min_val = INT_MAX, max_val = INT_MIN;
#endif
        

        // ... and now the columns
        int step_y = iceenv.max_sy / icecomp[i].sy;
		for (x = 0; x < icecomp[i].width; x++)
		{
			cur_srcimage = srcimage2 + x;
			outpixels = icecomp[i].pixels + x;
			int* start_index = outpixels;
			for (y = 0; y < icecomp[i].height; y += step_y)
			{
				register int pixel_avg = 0;
				int y2;
				for (y2 = 0; y2 < step_y; y2++)
				{
					pixel_avg += *cur_srcimage;
					cur_srcimage += icecomp[i].stride;
				}
				pixel_avg /= step_y;
                // Level shift
                pixel_avg -= 128;
                *outpixels = pixel_avg;
#ifdef _JPEG_ENCODER_DEBUG
                if (pixel_avg < min_val)
                    min_val = pixel_avg;
                if (pixel_avg > max_val)
                    max_val = pixel_avg;
#endif
				outpixels += icecomp[i].stride;
			}
			int last_val = *(outpixels - icecomp[i].stride);
			// fill rest of the buffer with value of bottommost pixel
			while (outpixels < start_index + (icecomp[i].stride * (new_height - 1)))
			{
				*outpixels = last_val;
				outpixels += icecomp[i].stride;
			}
		}
        
        icecomp[i].height = new_height;
        
#ifdef _JPEG_ENCODER_DEBUG
        printf("Component: %d (%dx%d), Min value = %d, Max value = %d\n", i, icecomp[i].width , icecomp[i].height, min_val, max_val);
#endif
        
		free(srcimage2);
	}
	free(iceenv.image);
	iceenv.image = 0;
}

static int encode_du(int comp, int du_x, int du_y)
{
    int *buffer = 0;
    int duOriginIndex = ((iceenv.cur_mcu_y * (icecomp[comp].sy << 3) + (du_y << 3)) * icecomp[comp].stride) + (iceenv.cur_mcu_x * (icecomp[comp].sx << 3) + (du_x << 3));
    
    // Create 8x8 block
    int x, y;
    for (y = 0; y < 8; y++)
    {
        buffer = &icecomp[comp].pixels[duOriginIndex + (icecomp[comp].stride * y)];
        memcpy(&iceenv.block[y * 8], buffer, 8 * sizeof(int));
    }
    
    // Perform DCT
    fdct(iceenv.block);
    
    // Zigzag reordering
    int *block = (int*) malloc(64 * sizeof(int));
    for (x = 0; x < 64; x++)
        block[jpeg_zz[x]] = iceenv.block[x];
    memcpy(iceenv.block, block, 64 * sizeof(int));
	
	free(block);
	block = 0;
    
    // Quantization
	for (y = 0; y < 8; y++)
	{
		for (x = 0; x < 8; x++)
		{
			register int value = iceenv.block[(y * 8) + x];
			// Right-shift rounds towards negative infinity, so we're gonna do our
			// computations with positive numbers and then put the sign back
			// after we're done
 			int sign = value < 0 ? -1 : 1;
			if (sign < 0)
 				value *= sign;
			value = UPSCALE(value);
			value /= jpeg_qtbl_selector[comp][(y * 8) + x];
			iceenv.block[(y * 8) + x] = sign * DESCALE(value);
		}
	}

	// Do Zero Run Length Coding for this block
	// After all MCUs have been processed, the Huffman tables will be
	// generated based on these values
	x = 0;

	int* end_pointer = iceenv.block + 64;
	while (!end_pointer[-1])
		end_pointer--;

	block = iceenv.block;

	do
	{
		byte zeros = 0;

		// count contiguous zeros, but stop at 16 because
		// that's the most consecutive zeros than can be encoded
		while (!(*block) && zeros < 15)
		{
			zeros++;
			block++;
		}
		icecomp[comp].rlc[icecomp[comp].rlc_index].info = zeros << 4;

		register int value = *block;
		byte category = find_category(value);
		icecomp[comp].rlc[icecomp[comp].rlc_index].info |= category;

		icecomp[comp].rlc[icecomp[comp].rlc_index].value.length = category;
		icecomp[comp].rlc[icecomp[comp].rlc_index].value.bits = get_bit_coding(value, category);

		icecomp[comp].rlc_index++;
		block++;
	} while (block != end_pointer);

	// Only put an EOB if we don't have a zero run at the end
	if (end_pointer != iceenv.block + 64)
	{
		icecomp[comp].rlc[icecomp[comp].rlc_index].info = 0; // EOB

		icecomp[comp].rlc[icecomp[comp].rlc_index].value.length = 0;
		icecomp[comp].rlc[icecomp[comp].rlc_index].value.bits = 0;

		icecomp[comp].rlc_index++;
	}


	print_block(iceenv.block);
    
    return ERR_OK;
}

static int encode(void)
{
    int i, sx, sy;

	for (i = 0; i < iceenv.num_components; i++)
	{
		icecomp[i].rlc = (struct jpeg_zrlc*) malloc(sizeof(struct jpeg_zrlc) * 0xFFFF);
		memset(icecomp[i].rlc, 0, sizeof(struct jpeg_zrlc) * 0xFFFF);
	}

    // Encode every MCU
    for (;;)
    {
        for (i = 0; i < iceenv.num_components; i++)
        {
            for (sy = 0; sy < icecomp[i].sy; sy++)
            {
                for (sx = 0; sx < icecomp[i].sx; sx++)
                {
                    // Encode single DU
                    
                    encode_du(i, sx, sy);

                }
            }
        }
		break;
        iceenv.cur_mcu_x++;
        if (iceenv.cur_mcu_x == iceenv.num_mcu_x)
        {
            iceenv.cur_mcu_x = 0;
            iceenv.cur_mcu_y++;
            if (iceenv.cur_mcu_y == iceenv.num_mcu_y)
                break;
        }
    }
    
    return ERR_OK;
}

int icejpeg_encode_init(const char *filename, unsigned char *image, int width, int height, int y_samp, int cb_samp, int cr_samp)
{
	memset(icecomp, 0, sizeof(struct jpeg_encode_component) * 3);

	strcpy(iceenv.outfile, filename);
	if ((!cb_samp && cr_samp) ||
		(!cr_samp && cb_samp))
	{
		return ERR_INVALID_NUMBER_OF_COMP;
	}

	if (!cb_samp && !cr_samp)
		iceenv.num_components = 1;
	else
		iceenv.num_components = 3;

	iceenv.width = width;
	iceenv.height = height;
	iceenv.max_sx = iceenv.max_sy = 0;
	iceenv.image = (int *)malloc(width * height * iceenv.num_components * sizeof(int));
	if (!iceenv.image)
		return ERR_OUT_OF_MEMORY;
    
    // Copy image to our buffer and perform YCbCr->RGB conversion
    int x, y;
    int *cur_image = iceenv.image;
    for (y = 0; y < height; y++)
    {
        for (x = 0; x < width; x++)
        {
            register int y = DESCALE(YR * image[0] + YG * image[1] + YB * image[2]);
            register int cb = DESCALE(CBR * image[0] + CBG * image[1] + CBB * image[2]) + 128;
            register int cr = DESCALE(CRR * image[0] + CRG * image[1] + CRB * image[2]) + 128;
     
            *cur_image++ = y;
            *cur_image++ = cb;
            *cur_image++ = cr;
            image += 3;
        }
    }

	icecomp[0].sx = y_samp;
	icecomp[0].sy = y_samp;
	icecomp[1].sx = cb_samp;
	icecomp[1].sy = cb_samp;
	icecomp[2].sx = cr_samp;
	icecomp[2].sy = cr_samp;

	int i = 0;
	for (; i < iceenv.num_components; i++)
	{
		if (icecomp[i].sx > iceenv.max_sx)
			iceenv.max_sx = icecomp[i].sx;
		if (icecomp[i].sy > iceenv.max_sy)
			iceenv.max_sy = icecomp[i].sy;
	}

	iceenv.mcu_width = iceenv.max_sx << 3;
	iceenv.mcu_height = iceenv.max_sy << 3;
	iceenv.num_mcu_x = (iceenv.width + iceenv.mcu_width - 1) / iceenv.mcu_width;
	iceenv.num_mcu_y = (iceenv.height + iceenv.mcu_height - 1) / iceenv.mcu_height;

	for (i = 0; i < iceenv.num_components; i++)
	{
		icecomp[i].width = (icecomp[i].sx << 3) * iceenv.num_mcu_x; // (width * icecomp[i].sx + iceenv.max_sx - 1) / iceenv.max_sx;
		icecomp[i].height = height; // (height * icecomp[i].sy + iceenv.max_sy - 1) / iceenv.max_sy;
		icecomp[i].stride = (icecomp[i].sx << 3) * iceenv.num_mcu_x;

															 
		//icecomp[i].pixels = (byte *)malloc(icecomp[i].stride * icecomp[i].height);
	}

	return ERR_OK;
}

int icejpeg_write(void)
{
	downsample();
    encode();
    
	return 0;
}

/*!
* \brief
* [icejpeg_encode_cleanup]
*
* \author matthias.gruen
* \date 2016/01/12
* [1/12/2016 matthias.gruen]
*/
void icejpeg_encode_cleanup()
{
	int i = 0;
	for (i = 0; i < iceenv.num_components; i++)
	{
		if (icecomp[i].pixels)
			free(icecomp[i].pixels);
	}

	if (iceenv.image)
		free(iceenv.image);

	for (i = 0; i < iceenv.num_components; i++)
	{
		if (icecomp[i].rlc)
			free(icecomp[i].rlc);
	}
}
