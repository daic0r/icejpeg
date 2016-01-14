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
	struct jpeg_zrlc **rlc;
	int rlc_index;
    int dc_code_count[17];
    int ac_code_count[257];
	// Which code has what length?
	byte dc_code_lengths[17];
	byte ac_code_lengths[257];
	// Number of codes of each length
	byte dc_code_length_count[32];
	byte ac_code_length_count[32];
    // sorted list of symbols to be encoded
    byte dc_huffval[16];
    byte ac_huffval[256];
    // DHT segment informatiom
    struct jpeg_dht dc_dht;
    struct jpeg_dht ac_dht;
	long rlc_count;
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
inline static byte find_category(int num)
{
	byte category = 0;
	while ((num < ((-1 << category) + 1)) || (num > (1 << category) - 1))
		category++;

	return category;
}

// Get the bit coding of an int value
inline static int get_bit_coding(int num, byte category)
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
			for (x = 0; x < iceenv.width - (step_x-1); x += step_x)
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
			for (y = 0; y < icecomp[i].height - (step_y-1); y += step_y)
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

    //print_block(iceenv.block);
    
	// Do Zero Run Length Coding for this block
	// After all MCUs have been processed, the Huffman tables will be
	// generated based on these values
	x = 0;

	int* end_pointer = iceenv.block + 64;
	while (!end_pointer[-1] && end_pointer > iceenv.block)
		end_pointer--;

	block = iceenv.block;

    int start_rlc_index = icecomp[comp].rlc_index;
	while (block < end_pointer)
	{
		byte zeros = 0;

		// count contiguous zeros, but stop at 16 because
		// that's the most consecutive zeros than can be encoded
		while (block > iceenv.block && !(*block) && zeros < 15)
		{
			zeros++;
			block++;
		}
        // Allocate memory
        icecomp[comp].rlc[icecomp[comp].rlc_index] = (struct jpeg_zrlc*) malloc(sizeof(struct jpeg_zrlc));
        
		icecomp[comp].rlc[icecomp[comp].rlc_index]->info = zeros << 4;

		register int value = *block;
		byte category = find_category(value);
		icecomp[comp].rlc[icecomp[comp].rlc_index]->info |= category;

		icecomp[comp].rlc[icecomp[comp].rlc_index]->value.length = category;
		icecomp[comp].rlc[icecomp[comp].rlc_index]->value.bits = get_bit_coding(value, category);

		icecomp[comp].rlc_index++;
        
        if (icecomp[comp].rlc_index == 0xFFFF * 10)
        {
            printf("OUT OF MEMORY: (%d,%d)[%d](%d,%d)\n", iceenv.cur_mcu_x, iceenv.cur_mcu_y, comp, du_x, du_y);
            getc(stdin);

        }
        
		block++;
    };
    
	// Only put an EOB if we don't have a zero run at the end
	if (end_pointer != iceenv.block + 64)
	{
#ifdef _JPEG_ENCODER_DEBUG
        //printf("Block prematurely terminated after %d entries.\n", end_pointer - iceenv.block);
#endif
        
        // Allocate memory
        icecomp[comp].rlc[icecomp[comp].rlc_index] = (struct jpeg_zrlc*) malloc(sizeof(struct jpeg_zrlc));

		icecomp[comp].rlc[icecomp[comp].rlc_index]->info = 0; // EOB

		icecomp[comp].rlc[icecomp[comp].rlc_index]->value.length = 0;
		icecomp[comp].rlc[icecomp[comp].rlc_index]->value.bits = 0;

		icecomp[comp].rlc_index++;
        
        if (icecomp[comp].rlc_index == 0xFFFF * 10)
        {
            printf("OUT OF MEMORY: (%d,%d)[%d](%d,%d)\n", iceenv.cur_mcu_x, iceenv.cur_mcu_y, comp, du_x, du_y);
            getc(stdin);
        }
	}

#ifdef _JPEG_ENCODER_DEBUG
//     if (icecomp[comp].rlc_index - start_rlc_index > 10)
//         printf("MCU: (%d,%d)[%d](%d,%d): %d indices added.\n", iceenv.cur_mcu_x, iceenv.cur_mcu_y, comp, du_x, du_y, icecomp[comp].rlc_index - start_rlc_index);
//     icecomp[comp].rlc_count += icecomp[comp].rlc_index - start_rlc_index;
#endif
    
    return ERR_OK;
}

static void get_code_stats(void)
{
    int i ;
    // Gather statistics about code occurrences
    for (i = 0; i < iceenv.num_components; i++)
    {
        struct jpeg_encode_component *c = &icecomp[i];
        int count = c->rlc_index;
        int j;
        int is_dc = 1;
        int du_index = 0;
        memset(c->dc_code_count, 0, 17 * sizeof(int));
        memset(c->ac_code_count, 0, 257 * sizeof(int));
        
        for (j = 0; j < count; j++)
        {
            if (is_dc)
            {
                c->dc_code_count[c->rlc[j]->info]++;
                is_dc = 0;
            }
            else
                c->ac_code_count[c->rlc[j]->info]++;
            
            du_index += (c->rlc[j]->info & 0xF) >> 4;
            du_index++;
            if (du_index == 64 || c->rlc[j]->info == 0)
            {
                du_index = 0;
                is_dc = 1;
            }
        }
        
		c->dc_code_count[16] = 1;
		c->ac_code_count[256] = 1;

#ifdef _JPEG_ENCODER_DEBUG
        
		if (i == 0)
		{
			printf("DC Stats comp #%d\n\n", i);

			for (j = 0; j < 16; j++)
			{
				if (c->dc_code_count[j] > 0)
					printf("Code 0x%X: %d occurrences\n", j, c->dc_code_count[j]);
			}
			printf("\n");
			printf("Final count: %ld\n\n", c->rlc_count);
		}
#endif
    }
}

static void find_code_lengths(void)
{
    byte dc_codelengths[17];
	byte ac_codelengths[257];
    int dc_others[17], ac_others[257];
    
	// Gather statistics about code occurrences
	get_code_stats();

	struct
	{
		int code;
		int freq;
	} v[2];

	int i;

	for (i = 0; i < iceenv.num_components; i++)
	{
		int dcac = 0;

		// pointers to current array
		byte *codelengths = 0;
		int *others = 0;
		int *codecount = 0;
		// Do DC and AC
		for (dcac = 0; dcac < 2; dcac++)
		{
			struct jpeg_encode_component *c = &icecomp[i];

			codelengths = !dcac ? dc_codelengths : ac_codelengths;
			others = !dcac ? dc_others : ac_others;
			codecount = !dcac ? c->dc_code_count : c->ac_code_count;
			int numcodes = !dcac ? 17 : 257;

			memset(codelengths, 0, numcodes);
			memset(others, -1, sizeof(int) * numcodes);

			// Proceed according to JPEG Standard Annex K, Figure K.1
			do
			{
				int  j;
				// Find least FREQ(V1) and FREQ(V2)
				for (j = 0; j < 2; j++)
				{
					// Find rarest code in DC frequency count table
					v[j].code = -1;
					v[j].freq = INT_MAX;

					int code_index = 0;
					for (code_index = 0; code_index < numcodes; code_index++)
					{
						if (j == 1 && code_index == v[0].code)
							continue;
						if (codecount[code_index] && codecount[code_index] <= v[j].freq && code_index > v[j].code)
						{
							v[j].code = code_index;
							v[j].freq = codecount[code_index];
						}
					}
				}

				// No V2? Break
				if (v[1].freq == INT_MAX)
					break;

				codecount[v[0].code] += codecount[v[1].code];
				codecount[v[1].code] = 0;

				while (1)
				{
					codelengths[v[0].code] += 1;

					if (others[v[0].code] == -1)
						break;

					v[0].code = others[v[0].code];
				}

				others[v[0].code] = v[1].code;

				while (1)
				{
					codelengths[v[1].code] += 1;

					if (others[v[1].code] == -1)
						break;

					v[1].code = others[v[1].code];
				}
			} while (others[v[1].code] == -1);

			// AT THIS POINT WE HAVE THE CODE LENGTH FOR EACH SYMBOL STORED IN codelengths

			memcpy(!dcac ? c->dc_code_lengths : c->ac_code_lengths, codelengths, numcodes);

			// NOW WE COUNT THE NUMBER OF CODES OF EACH LENGTH
			// WE CAN POTENTIALLY HAVE CODES OF UP TO 32 BITS AT THIS POINT
			byte num_codes_of_each_length[33];
			memset(num_codes_of_each_length, 0, sizeof(num_codes_of_each_length));
			int j = 0;
			for (j = 0; j < numcodes; j++)
			{
				if (codelengths[j])
					num_codes_of_each_length[codelengths[j]]++;
			}

			memcpy(!dcac ? c->dc_code_length_count : c->ac_code_length_count, num_codes_of_each_length, sizeof(num_codes_of_each_length));

#ifdef _JPEG_ENCODER_DEBUG

			if (i == 0 && dcac == 0)
			{
				//{
					//int j = 0;
				for (j = 0; j < numcodes; j++)
				{
					printf("Code size of symbol %X = %d\n", j, !dcac ? c->dc_code_lengths[j] : c->ac_code_lengths[j]);
				}
				printf("\n");
				//}



				for (j = 0; j < 33; j++)
				{
					printf("Number of codes of length %d : %d\n", j, num_codes_of_each_length[j]);
				}

			}
#endif
		}
	}
}

static void limit_code_lengths()
{
	printf("LIMITING\n\n");

	int ncomp = 0;
	for (ncomp = 0; ncomp < iceenv.num_components; ncomp++)
	{
		struct jpeg_encode_component *c = &icecomp[ncomp];
		byte *code_length_count = 0;
		int dcac = 0;
		for (dcac = 0; dcac < 2; dcac++)
		{
			code_length_count = !dcac ? c->dc_code_length_count : c->ac_code_length_count;

			int i = 32, j;
			
			while (1)
			{
				if (!code_length_count[i])
				{
					i--;
					if (i == 16)
					{
						// Below 16 we work our way down until we find a code length that is used
						while (!code_length_count[i])
							i--;

						// This will be the maximum code length in use
						// One must be subtracted from it because the "fake" code
						// added during
						// c->dc_code_count[16] = 1;
						// c->ac_code_count[256] = 1;
						// must be removed from the count
						code_length_count[i]--;

						break;
					}
					else
					{
						continue;
					}
				}

				j = i - 1;
				j--;
				while (!code_length_count[j])
					j--;

				code_length_count[i] -= 2;
				code_length_count[i - 1]++;
				code_length_count[j + 1] += 2;
				code_length_count[j]--;
			}

#ifdef _JPEG_ENCODER_DEBUG
			if (ncomp == 0 && dcac == 0)
			{
				int dbg = 0;
				for (dbg = 0; dbg < 32; dbg++)
				{
					printf("Number of codes of length %d : %d\n", dbg, code_length_count[dbg]);
				}
			}
#endif
		}
	}
}

// static void sort_codes()
//
// Here we generate a sorted list of symbols
// sort criterion is the symbol's VLC code length
static void sort_codes()
{
	byte dc_huffval[16];
	byte ac_huffval[256];

	int ncomp = 0;
	for (ncomp = 0; ncomp < iceenv.num_components; ncomp++)
	{
		struct jpeg_encode_component *c = &icecomp[ncomp];
		byte *codelengths = 0;
		byte *huffval = 0;
		int dcac = 0;
		int numcodes = 0;
		for (dcac = 0; dcac < 2; dcac++)
		{
			codelengths = !dcac ? c->dc_code_lengths : c->ac_code_lengths;
			huffval = !dcac ? dc_huffval : ac_huffval;
			numcodes = !dcac ? 16 : 256;

			memset(huffval, 0, sizeof(byte) * numcodes);

			int i = 1, j, k = 0;

			while (i < 33)
			{
				j = 0;
				while (j < numcodes)
				{
					if (codelengths[j] == i)
						huffval[k++] = j;

					j++;
				}
				i++;
			}
			
            memcpy(!dcac ? c->dc_huffval : c->ac_huffval, huffval, sizeof(byte) * numcodes);
            
#ifdef _JPEG_ENCODER_DEBUG
			if (ncomp == 0 && dcac == 0)
			{

				for (j = 0; j < numcodes; j++)
				{
					printf("Position %d -> Code %d\n", j, huffval[j]);
				}
				printf("\n");
			}
#endif

		}
	}
}

// generates an array huffsize that contains
// the length of the code that would be written
// at each index
static void gen_huffman_code_sizes()
{
    int dc_huffsize[256];
    int ac_huffsize[256];
    
    int ncomp = 0;
    for (ncomp = 0; ncomp < iceenv.num_components; ncomp++)
    {
        struct jpeg_encode_component *c = &icecomp[ncomp];
        byte *codelength_count = 0;
        int dcac = 0;
        int numcodes = 0;
        int *huffsize = 0;
        for (dcac = 0; dcac < 2; dcac++)
        {
            codelength_count = !dcac ? c->dc_code_length_count : c->ac_code_length_count;
            huffsize = !dcac ? dc_huffsize : ac_huffsize;
            numcodes = !dcac ? 16 : 256;
            int lastk = 0;
            
            memset(huffsize, 0, sizeof(int) * 256);
            
            int k = 0, i = 1, j = 1;
            
            while (i <= 15)
            {
                while (j <= codelength_count[i])
                {
                    huffsize[k++] = i;
                    j++;
                }
            
                i++;
                j = 1;
            }
            
            huffsize[k] = 0;
            lastk = k;
            
#ifdef _JPEG_ENCODER_DEBUG
            if (ncomp == 0 && dcac == 0)
            {
                
                for (j = 0; j < 256; j++)
                {
                    printf("Position %d -> Value %d\n", j, huffsize[j]);
                }
                printf("\n");
            }
#endif
        }
    }
    
}


// Here we create the JPEG style DHT data
// which can be used by the function get_huffman_tables() from the
// decoder to generate out bitstrings
static void gen_DHT(void)
{
    int ncomp = 0;
    for (ncomp = 0; ncomp < iceenv.num_components; ncomp++)
    {
        struct jpeg_encode_component *c = &icecomp[ncomp];
        int dcac = 0;
        int numcodes = 0;
        struct jpeg_dht *dht = 0;
        byte *codelength_count = 0;
        for (dcac = 0; dcac < 2; dcac++)
        {
            dht = !dcac ? &c->dc_dht : &c->ac_dht;
            codelength_count = !dcac ? c->dc_code_length_count : c->ac_code_length_count;
            numcodes = !dcac ? 16 : 256;
            
            byte codes_total = 0;
            int i = 0;
            for (i = 0; i < 16; i++)
            {
                dht->num_codes[i] = codelength_count[i+1];
                codes_total += dht->num_codes[i];
            }
            
            dht->codes = (byte*) malloc(codes_total);
            memcpy(dht->codes, !dcac ? c->dc_huffval : c->ac_huffval, codes_total);
        }
    }
}

static int encode(void)
{
    int i, sx, sy;

	for (i = 0; i < iceenv.num_components; i++)
	{
		icecomp[i].rlc = (struct jpeg_zrlc**) malloc(sizeof(struct jpeg_zrlc*) * 0xFFFF * 10);
		memset(icecomp[i].rlc, 0, sizeof(struct jpeg_zrlc*) * 0xFFFF * 10);
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
        //break;
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
	find_code_lengths();
	//limit_code_lengths();
	sort_codes();
    //gen_huffman_code_sizes();
    gen_DHT();

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
	int i = 0, j = 0;
	for (i = 0; i < iceenv.num_components; i++)
	{
		if (icecomp[i].pixels)
			free(icecomp[i].pixels);
	}

	if (iceenv.image)
		free(iceenv.image);

	for (i = 0; i < iceenv.num_components; i++)
	{
        j = 0;
        while (icecomp[i].rlc[j])
            free(icecomp[i].rlc[j++]);
		if (icecomp[i].rlc)
			free(icecomp[i].rlc);
	}
}
