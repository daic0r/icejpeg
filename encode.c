#include "encode.h"
#include "common.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>

#define _JPEG_ENCODER_DEBUG

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

struct __ice_env
{
	const char outfile[40];
	int *image;
	int width, height;
	int num_components;
	int max_sx, max_sy;
	int num_mcu_x, num_mcu_y;
	int mcu_width, mcu_height;
} iceenv;

struct jpeg_encode_component icecomp[3];

static void generate_constants()
{
	int y_r = round(0.299f * (1 << PRECISION));
	int y_g = round(0.587f * (1 << PRECISION));
	int y_b = round(0.114f * (1 << PRECISION));

	int cb_r = round(-0.1687f * (1 << PRECISION));
	int cb_g = round(-0.3313f * (1 << PRECISION));
	int cb_b = round(0.5f * (1 << PRECISION));

	int cr_r = round(0.5f * (1 << PRECISION));
	int cr_g = round(-0.4187f * (1 << PRECISION));
	int cr_b = round(-0.0813f * (1 << PRECISION));

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

static void convert_and_downsample()
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
				*outpixels++ = last_val;
		}

		int *tmpimage2 = icecomp[i].pixels;
        int new_height = ((icecomp[i].sy << 3) * iceenv.num_mcu_y);
		icecomp[i].pixels = (int*)malloc(icecomp[i].stride * new_height * sizeof(int));

		outpixels = icecomp[i].pixels;
		int *cur_tmpimage = 0;

#ifdef _JPEG_ENCODER_DEBUG
        int min_val = INT_MAX, max_val = INT_MIN;
#endif
        
		// ... and now the columns
		for (x = 0; x < icecomp[i].width; x++)
		{
			cur_tmpimage = tmpimage2 + x;
			outpixels = icecomp[i].pixels + x;
			int* start_index = outpixels + x;
			int step_y = iceenv.max_sy / icecomp[i].sy;
			for (y = 0; y < icecomp[i].height; y += step_y)
			{
				register int pixel_avg = 0;
				int y2;
				for (y2 = 0; y2 < step_y; y2++)
				{
					pixel_avg += *cur_tmpimage;
					cur_tmpimage += icecomp[i].stride;
				}
				pixel_avg /= step_y;
                // Downshift
                pixel_avg -= 128;
                *outpixels = pixel_avg;
#ifdef _JPEG_ENCODER_DEBUG
                if (pixel_avg < min_val)
                    min_val = pixel_avg;
                if (pixel_avg > max_val)
                    max_val = pixel_avg;
#endif
//				pixel_avg = clip(pixel_avg);
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
        
		free(tmpimage2);
	}
}

int icejpeg_encode_init(const char *filename, unsigned char *image, int width, int height, int y_samp, int cb_samp, int cr_samp)
{
	memset(icecomp, 0, sizeof(struct jpeg_component) * 3);

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
	convert_and_downsample();

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
}
