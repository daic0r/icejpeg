#include "encode.h"
#include "common.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define DESCALE(x) ((x) + (PRECISION >> 1))

struct __ice_env
{
	const char outfile[40];
	byte *image;
	int width, height;
	int num_components;
	int max_sx, max_sy;
	int num_mcu_x, num_mcu_y;
	int mcu_width, mcu_height;
} iceenv;

struct jpeg_component icecomp[3];

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

static void convert_colorspace()
{
	int i = 0;
	for (i = 0; i < iceenv.num_components; i++)
	{

	}
}

int icejpeg_encode_init(const char *filename, unsigned char *image, int width, int height, int y_samp, int cb_samp, int cr_samp)
{
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

	memset(icecomp, 0, sizeof(struct jpeg_component) * 3);

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

	iceenv.image = 0;
	iceenv.mcu_width = iceenv.max_sx << 3;
	iceenv.mcu_height = iceenv.max_sy << 3;
	iceenv.num_mcu_x = (iceenv.width + iceenv.mcu_width - 1) / iceenv.mcu_width;
	iceenv.num_mcu_y = (iceenv.height + iceenv.mcu_height - 1) / iceenv.mcu_height;

	for (i = 0; i < iceenv.num_components; i++)
	{
		icecomp[i].width = (icecomp[i].sx << 3) * iceenv.num_mcu_x;
		icecomp[i].height = (icecomp[i].sy << 3) * iceenv.num_mcu_y;
		icecomp[i].stride = icecomp[i].width;
		icecomp[i].pixels = (byte *)malloc(icecomp[i].stride * icecomp[i].height * sizeof(byte));
	}

	return ERR_OK;
}

int icejpeg_write(void)
{
	

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
}
