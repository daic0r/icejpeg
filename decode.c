//  *************************************************************************************
//
//  decode.c
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
//
//  This file constitutes the decoder part of my IceJPEG library, which was
//  written mainly because I wanted to understand the inner workings of the
//  JPEG format.
//
//  Currently, only baseline JPEGs are supported. However, I would like to implement
//  progressive decompressiom as well at some point.
//
//  Only grayscale and RGB JPEGs can be decoded using this code.
//
//  Restart markers are supported also.
//
//  The code that performs the IDCT (Inverse Discrete Cosine Transform) was taken from
//  libmpeg2, with slight modifications.
//
//  *************************************************************************************

#include <stdio.h>
#include <memory.h>
#include <sys/stat.h>
#include <stdlib.h>
#include "common.h"
#include "IDCT.h"
#include "upsample.h"
#include "decode.h"
#include <string.h>


//#define _JPEG_DEBUG
#define USE_LANCZOS_UPSAMPLING

typedef struct jpeg_huffman_code** jpeg_huffman_table;	// array of pointers to a huffman code
typedef byte* jpeg_dqttable;

static struct __ice_decode_env
{
	byte *buffer;
	int buf_pos;
	word cur_segment_len;

	byte cur_byte_remaining;

	byte max_samp_x, max_samp_y;
	int eoi;
	int block[64];
	int mcu_width, mcu_height;
	int num_mcu_x, num_mcu_y;
	int cur_mcu_x, cur_mcu_y;
	int cur_du_x, cur_du_y;
	int restart_interval;
	byte next_rst_marker;
	int rstcount;
	byte *image;
	struct jpeg_component *components;

	jpeg_huffman_table huff_dc[MAX_DC_TABLES];
	jpeg_huffman_table huff_ac[MAX_AC_TABLES];

	struct jpeg_dht *dc_dht[MAX_DC_TABLES];
	struct jpeg_dht *ac_dht[MAX_AC_TABLES];

	jpeg_dqttable qt_tables[4];

	struct jpeg_app0 app0;

	// as read from the file
	struct jpeg_sof0 sof0;
} iceenv;

#pragma pack(push)
#pragma pack(1)





#pragma pack(pop)

void cleanup(void);
int process_segment(void);
void cleanup_dht(void);

int icejpeg_decode_init(const char* filename)
{
	memset(&iceenv, 0, sizeof(struct __ice_decode_env));

	FILE *file; // Our jpeg file
    file = fopen(filename, "rb");
    if (!file)
        return ERR_OPENFILE_FAILED;
    
	struct stat st; // file stats
    fstat(fileno(file), &st);
    
    iceenv.buffer = (byte *)malloc(sizeof(byte) * st.st_size);
    fread((void*)iceenv.buffer, sizeof(byte), st.st_size, file);
    
#ifdef _JPEG_DEBUG
    printf("%lld bytes read.\n", st.st_size);
    printf("%d\n", ferror(file));
#endif
    
    fclose(file);
    
    if (iceenv.buffer[iceenv.buf_pos++] != 0xFF || iceenv.buffer[iceenv.buf_pos++] != 0xD8)
    {
        cleanup();
        return ERR_NO_JPEG;
    }
    
    int i;
    for (i = 0; i < MAX_DC_TABLES; i++)
    {
        iceenv.dc_dht[i] = 0;
        iceenv.huff_dc[i] = 0;
    }
    for (i = 0; i < MAX_AC_TABLES; i++)
    {
        iceenv.ac_dht[i] = 0;
        iceenv.huff_ac[i] = 0;
    }
    
    for (i = 0; i < 4; i++)
    {
        iceenv.qt_tables[i] = 0;
    }
    
    iceenv.restart_interval = 0;
	iceenv.cur_byte_remaining = 8;
    
    return ERR_OK;
}

int icejpeg_read(unsigned char **buffer, int *width, int *height, int *num_components)
{
    int err;
    while (!iceenv.eoi)
    {
        err = process_segment();
        if (err != ERR_OK)
            return err;
    }
    
    *buffer = iceenv.image;
    *width = iceenv.sof0.width;
    *height = iceenv.sof0.height;
    *num_components = iceenv.sof0.num_components;
    
    return ERR_OK;
}

void icejpeg_cleanup(void)
{
    cleanup();
}

//////////////////////////////////////////////////////////////////////////
// Helper functions
//////////////////////////////////////////////////////////////////////////

word fetch_word(void)
{
    word temp;
    temp = iceenv.buffer[iceenv.buf_pos++];
    temp <<= 8;
    temp |= iceenv.buffer[iceenv.buf_pos++];
    return temp;
}

int gen_huffman_tables(void)
{
    int i, j, k;
    
    struct jpeg_dht* cur_src_table = 0;
    jpeg_huffman_table cur_dst_table = 0;
    
    // Loop over all 2 tables
    for (i = 0; i < MAX_DC_TABLES + MAX_AC_TABLES; i++)
    {
        if (i >= 0 && i < MAX_DC_TABLES)
        {
            cur_src_table = iceenv.dc_dht[i];
        }
        else
        {
            cur_src_table = iceenv.ac_dht[i - MAX_DC_TABLES];
        }
        
        if (!cur_src_table)
            continue;
        
        word cur_bitstring = 0;
        byte cur_length = 0;
        byte code_buf_pos = 0;
        
        
        cur_dst_table = (jpeg_huffman_table)malloc(sizeof(struct jpeg_huffman_code*) * 0xFFFF);
        memset((void*)cur_dst_table, 0, sizeof(struct jpeg_huffman_code*) * 0xFFFF);
        
        if (i >= 0 && i < MAX_DC_TABLES)
        {
            iceenv.huff_dc[i] = cur_dst_table;
        }
        else
        {
            iceenv.huff_ac[i - MAX_DC_TABLES] = cur_dst_table;
        }
        
        // Loop over all 16 code lengths
        for (j = 0; j < 16; j++)
        {
            cur_length = j+1;
            
#ifdef _JPEG_DEBUG
            printf("Codes of length %d bits:\n", cur_length);
#endif
            // Loop over all codes of length j
            for (k = 0; k < cur_src_table->num_codes[j]; k++)
            {
                cur_dst_table[cur_bitstring] = (struct jpeg_huffman_code*)malloc(sizeof(struct jpeg_huffman_code));
                cur_dst_table[cur_bitstring]->length = cur_length;
                cur_dst_table[cur_bitstring]->code = cur_src_table->codes[code_buf_pos++];
                
#ifdef _JPEG_DEBUG
                printf("\t");
                int l;
                for (l = cur_length-1; l >= 0; l--)
                {
                    printf("%d", (cur_bitstring & (1 << l)) >> l);
                }
                printf(" -> %X\n", cur_dst_table[cur_bitstring]->code);
#endif
                
                cur_bitstring++;
                
            }
            cur_bitstring <<= 1;
        }
#ifdef _JPEG_DEBUG
        printf("\n");
        getc(stdin);
#endif
    }
    
    cleanup_dht();
    
    return ERR_OK;
}

word fetch_bits(int num_bits)
{
    word result = 0;
    byte bits_from_cur_byte = 0;
    while (num_bits > 0)
    {
        byte mask = 0;
        bits_from_cur_byte = min(iceenv.cur_byte_remaining, num_bits);
        byte mask_shift = iceenv.cur_byte_remaining - bits_from_cur_byte;
        
        mask = (1 << bits_from_cur_byte) - 1;
        //mask <<= mask_shift;
        result <<= bits_from_cur_byte;
        result |= (iceenv.buffer[iceenv.buf_pos] >> mask_shift) & mask;
        
        iceenv.cur_byte_remaining = max(0, iceenv.cur_byte_remaining - num_bits);
        num_bits -= bits_from_cur_byte;
        
        if (!iceenv.cur_byte_remaining && num_bits)
        {
            if (iceenv.buffer[iceenv.buf_pos] == 0xFF)
            {
                switch (iceenv.buffer[iceenv.buf_pos + 1])
                {
                    case 0x00:
                        iceenv.buf_pos++;
                        break;
                    default:
                        printf("ERROR: Marker detected in bit stream!\n");
                        getc(stdin);
                        break;
                }
            }
            iceenv.cur_byte_remaining = 8;
            iceenv.buf_pos++;
            // Skip stuff bytes
//            if (skip_stuff_byte)
//            {
//                iceenv.buf_pos++;
//                skip_stuff_byte = 0;
//            }
//            if (iceenv.scan_buffer[iceenv.buf_pos] == 0xFF)
//            {
//                switch (iceenv.scan_buffer[iceenv.buf_pos + 1])
//                {
//                    case 0x00:
//                        skip_stuff_byte = 1;
//                        break;
//                    default:
//                        printf("ERROR: Marker detected in bit stream!\n");
//                        getc(stdin);
//                        break;
//                }
//            }
        }
    }
    
    return result;
}

// Determines the value for a given huffman code
int get_huffman_code(word bitstring, byte length, jpeg_huffman_table table)
{
    if (table[bitstring] && table[bitstring]->length == length)
    {
        return table[bitstring]->code;
    }
    return -1;
}

// Fetches the next huffman code from the bitstream
byte get_next_code(jpeg_huffman_table cur_table)
{
    word bit_string = 0;
    byte cur_length = 1;
    byte cur_code = 0;
    int found = 0;
    
    while (cur_length < 17)
    {
        bit_string <<= 1;
        bit_string |= fetch_bits(1);
        int code = get_huffman_code(bit_string, cur_length, cur_table);
        if (code > -1)
        {
            cur_code = code;
            found = 1;
            break;
        }
        cur_length++;
    }
    
    if (!found)
    {
        printf("No code found!\n");
        getc(stdin);
    }
    
    return cur_code;
}

short get_signed_short(word bit_string, byte length)
{
    return bit_string & (1 << (length - 1)) ? bit_string : (-1 << length) + 1 + bit_string;
}

int process_app0(void)
{
    memcpy((void*)&iceenv.app0, (void*) (iceenv.buffer + iceenv.buf_pos), sizeof(struct jpeg_app0));
    
    if (strcmp(iceenv.app0.strjfif, "JFIF"))
        return ERR_INVALID_JFIF_STRING;
    
    if (iceenv.app0.maj_revision != 1)
        return ERR_INVALID_MAJOR_REV;
    
    iceenv.buf_pos += iceenv.cur_segment_len - 2;
    
    return ERR_OK;
}

int process_dqt(void)
{
    int bytes_read = 2;
    
    while (bytes_read < iceenv.cur_segment_len)
    {
        byte info = iceenv.buffer[iceenv.buf_pos++];
        // 16bit?
        if (UPR4(info))
            return ERR_16BIT_DQT_NOT_SUPPORTED;
        
        iceenv.qt_tables[LWR4(info)] = (byte *)malloc(64);
        memcpy((void*)iceenv.qt_tables[LWR4(info)], (void*)(iceenv.buffer + iceenv.buf_pos), 64);
        iceenv.buf_pos += 64;
        bytes_read += 64 + 1;	// including the info byte above
        
#ifdef _JPEG_DEBUG
        int x = 0, y = 0;
        for (y = 0; y < 8; y++)
        {
            for (x = 0; x < 8; x++)
            {
                printf("%d ", iceenv.qt_tables[info & 0xF][(y * 8) + x]);
            }
            printf("\n");
        }
#endif
    }

    
    return ERR_OK;
}

int process_sof0(void)
{
    iceenv.max_samp_y = iceenv.max_samp_x = 0;
    
    memcpy((void*)&iceenv.sof0, (void*)(iceenv.buffer + iceenv.buf_pos), sizeof(struct jpeg_sof0));
    iceenv.buf_pos += sizeof(struct jpeg_sof0);
    
    if (iceenv.sof0.num_components != 1 && iceenv.sof0.num_components != 3)
        return ERR_INVALID_NUMBER_OF_COMP;
    
	iceenv.sof0.width = FLIP(iceenv.sof0.width);
	iceenv.sof0.height = FLIP(iceenv.sof0.height);
    
    struct jpeg_sof0_component_info comp_info[3];
    //comp_info[0] = comp_info[1] = comp_info[2] = 0;
    
    int i;
    // 	for (i = 0; i < sof0.num_components; i++)
    // 	{
    // 		comp_info[i] = (struct jpeg_sof0_component_info*)malloc(sizeof(struct jpeg_sof0_component_info));
    // 	}
    
	iceenv.components = (struct jpeg_component*) malloc(sizeof(struct jpeg_component) * iceenv.sof0.num_components);
    
    for (i = 0; i < iceenv.sof0.num_components; i++)
    {
        memcpy((void*)&comp_info[i], (void*)(iceenv.buffer + iceenv.buf_pos), sizeof(struct jpeg_sof0_component_info));
        iceenv.buf_pos += sizeof(struct jpeg_sof0_component_info);
        
		iceenv.components[i].qt_table = comp_info[i].qt_table;
		iceenv.components[i].sx = UPR4(comp_info[i].sampling_factors);
		iceenv.components[i].sy = LWR4(comp_info[i].sampling_factors);
		iceenv.components[i].prev_dc = 0;
        
        // Update maximum sampling factors
        if (iceenv.components[i].sx > iceenv.max_samp_x)
            iceenv.max_samp_x = iceenv.components[i].sx;
        if (iceenv.components[i].sy > iceenv.max_samp_y)
            iceenv.max_samp_y = iceenv.components[i].sy;
        
    }
    
	iceenv.mcu_width = iceenv.max_samp_x << 3;
	iceenv.mcu_height = iceenv.max_samp_y << 3;
    iceenv.num_mcu_x = (iceenv.sof0.width + iceenv.mcu_width - 1) / iceenv.mcu_width;
    iceenv.num_mcu_y = (iceenv.sof0.height + iceenv.mcu_height - 1) / iceenv.mcu_height;
    
    for (i = 0; i < iceenv.sof0.num_components; i++)
    {
		iceenv.components[i].width = (iceenv.sof0.width * iceenv.components[i].sx + iceenv.max_samp_x - 1) / iceenv.max_samp_x;
		iceenv.components[i].height = (iceenv.sof0.height * iceenv.components[i].sy + iceenv.max_samp_y - 1) / iceenv.max_samp_y;
		iceenv.components[i].stride = iceenv.num_mcu_x * (iceenv.components[i].sx << 3);
		iceenv.components[i].pixels = (byte*)malloc(iceenv.components[i].stride * (iceenv.num_mcu_y * (iceenv.components[i].sy << 3)) * sizeof(byte));
    }
    
#ifdef _JPEG_DEBUG
    printf("Hmax = %d, Vmax = %d\n", iceenv.max_samp_x, iceenv.max_samp_y);
#endif
    
    return ERR_OK;
}

int process_dht(void)
{
    int bytes_read = 2;
    
    while (bytes_read < iceenv.cur_segment_len)
    {
        byte info = iceenv.buffer[iceenv.buf_pos++];
        byte type_table = info & 0x10;
        struct jpeg_dht *cur_table;
        
        if (!type_table)
            //cur_table = iceenv.dc_dht;
        {
            iceenv.dc_dht[LWR4(info)] = (struct jpeg_dht*)malloc(sizeof(struct jpeg_dht));
            cur_table = iceenv.dc_dht[LWR4(info)];
        }
        else
        {
            //	cur_table = iceenv.ac_dht;
            iceenv.ac_dht[LWR4(info)] = (struct jpeg_dht*)malloc(sizeof(struct jpeg_dht));
            cur_table = iceenv.ac_dht[LWR4(info)];
        }
        
        //cur_table[info & 0xF] = (struct jpeg_dht*)malloc(sizeof(struct jpeg_dht));
        memcpy((void*)cur_table->num_codes, (void*)(iceenv.buffer + iceenv.buf_pos), 16);
        iceenv.buf_pos += 16;
        bytes_read += 16 + 1;
        
        int num_codes = 0;
        int i = 0;
        for (i = 0; i < 16; i++)
        {
            num_codes += cur_table->num_codes[i];
        }
        cur_table->codes = (byte *)malloc(num_codes);
        memcpy((void*)cur_table->codes, (void*)(iceenv.buffer + iceenv.buf_pos), num_codes);
        
#ifdef _JPEG_DEBUG
        for (i = 0; i < num_codes; i++)
        {
            printf("%X ", cur_table->codes[i]);
        }
        printf("\n");
#endif
        
        iceenv.buf_pos += num_codes;
        bytes_read += num_codes;
    }
    
    return ERR_OK;
}

int process_sos(void)
{
    byte num_components = iceenv.buffer[iceenv.buf_pos++];
    if (iceenv.cur_segment_len != 6 + 2 * num_components)
        return ERR_INVALID_SEGMENT_SIZE;
    
    if (!iceenv.components)
        return ERR_SOF0_MISSING;
    
    int i = 0;
    for (i = 0; i < num_components; i++)
    {
        byte id = iceenv.buffer[iceenv.buf_pos++];
		iceenv.components[id-1].id_dht = iceenv.buffer[iceenv.buf_pos++];
    }
    
    // Ignore the following 3 bytes
    iceenv.buf_pos += 3;
    
    return ERR_OK;
}

int process_dri(void)
{
    if (iceenv.cur_segment_len != 4)
        return ERR_INVALID_SEGMENT_SIZE;
    
    iceenv.restart_interval = fetch_word();
    iceenv.rstcount = iceenv.restart_interval;
    
    return ERR_OK;
}

// Decode a single DU within an MCU
int decode_du(byte id_component)
{
    word bit_string = 0;
    byte cur_code = 0;
    
    memset(iceenv.block, 0, sizeof(int) * 64);
    
    jpeg_huffman_table cur_table = iceenv.huff_dc[UPR4(iceenv.components[id_component].id_dht)];
    
    cur_code = get_next_code(cur_table);
    
#ifdef _JPEG_DEBUG
    printf("Code found: %X\n", cur_code);
#endif
    
    bit_string = fetch_bits(cur_code);
    
#ifdef _JPEG_DEBUG
    printf("Bits fetched: %d\n", bit_string);
#endif
    
    short value = get_signed_short(bit_string, cur_code);
    
	iceenv.components[id_component].prev_dc += value;
    //mcu->dus[id_component][(y*samp_x) + x][0] = cur_mcu > 0 ? (mcus[cur_mcu - 1].dus[id_component][(y*samp_x) + x][0] + dc_value) : dc_value;
	iceenv.block[0] = iceenv.components[id_component].prev_dc;
    
#ifdef _JPEG_DEBUG
    printf("DC value: %d, absolute value: %d\n", block[0], value);
#endif
    
    // Dequantize DC value
	iceenv.block[0] *= iceenv.qt_tables[iceenv.components[id_component].qt_table][0];
    
    // Switch to AC table
    cur_table = iceenv.huff_ac[LWR4(iceenv.components[id_component].id_dht)];
    
    byte block_index = 1;
    
    int had_eob = 0;
    
    while (block_index < 64)
    {
        cur_code = get_next_code(cur_table);
        
        if (cur_code == 0)
        {
#ifdef _JPEG_DEBUG
    printf("\tEOB encountered\n");
#endif
            had_eob = 1;
            break;
        }
        
#ifdef _JPEG_DEBUG
    printf("\tSkipping %d zeros\n", (cur_code & 0xF0) >> 4);
#endif
        
        // Skip zeros
        block_index += UPR4(cur_code);
        if (block_index > 63)
            break;
        
        bit_string = fetch_bits(LWR4(cur_code));
        
#ifdef _JPEG_DEBUG
    printf("Fetched %d bits\n", cur_code & 0xF);
#endif
        
        value = get_signed_short(bit_string, LWR4(cur_code));
        
#ifdef _JPEG_DEBUG
    printf("AC value: %d\n", value);
#endif
        
        byte actual_index = jpeg_zzright[block_index];
        
        
        // Dequantize and unzigzag at the same time
		iceenv.block[actual_index] = value * iceenv.qt_tables[iceenv.components[id_component].qt_table][block_index];
        block_index++;
        
#ifdef _JPEG_DEBUG
    printf("Dequantized AC value: %d\n", block[actual_index]);
#endif
    }
    
    if (block_index > 64)
    {
        printf("CAUTION: Too many coefs in MCU [%d,%d]\n", iceenv.cur_mcu_x, iceenv.cur_mcu_y);
        getc(stdin);
    }
    
#ifdef _JPEG_DEBUG
    printf("\n");
#endif
    
    /****************************************************/
    /* Perform IDCT                                     */
    /****************************************************/
    int rowscols;
    for (rowscols = 0; rowscols < 8; rowscols++)
    {
        idctrow(&iceenv.block[8 * rowscols]);
    }
    for (rowscols = 0; rowscols < 8; rowscols++)
    {
        int targetPos = ((iceenv.cur_mcu_y * (iceenv.components[id_component].sy << 3) + (iceenv.cur_du_y << 3)) * iceenv.components[id_component].stride) + (iceenv.cur_mcu_x * (iceenv.components[id_component].sx << 3) + (iceenv.cur_du_x << 3));
        idctcol(&iceenv.block[rowscols], &iceenv.components[id_component].pixels[targetPos + rowscols], iceenv.components[id_component].stride);
    }
    
    return ERR_OK;
}

int decode_mcu(void)
{
    int comp;
    
    // Iterate over components (Y, Cb, Cr)
    for (comp = 0; comp < iceenv.sof0.num_components; comp++)
    {
        // Iterate over sampling factors
        for (iceenv.cur_du_y = 0; iceenv.cur_du_y < iceenv.components[comp].sy; iceenv.cur_du_y++)
        {
            for (iceenv.cur_du_x = 0; iceenv.cur_du_x < iceenv.components[comp].sx; iceenv.cur_du_x++)
            {
                decode_du(comp);
            }
        }
    }
    
    return ERR_OK;
}

int process_rst(void)
{
    iceenv.buf_pos++;
    iceenv.cur_byte_remaining = 8;
    
    if (iceenv.buffer[iceenv.buf_pos++] != 0xFF || (LWR4(iceenv.buffer[iceenv.buf_pos++])) != iceenv.next_rst_marker)
    {
        return ERR_INVALID_RST_MARKER;
    }
    
    iceenv.next_rst_marker = (iceenv.next_rst_marker + 1) & 7;
    
    iceenv.rstcount = iceenv.restart_interval;
    
    int i = 0;
    for (;i<iceenv.sof0.num_components;i++)
		iceenv.components[i].prev_dc = 0;
    
    return ERR_OK;
}

int decode_scan(void)
{
#ifdef _JPEG_DEBUG
    printf("%d MCUs in total.\n", iceenv.num_mcu_x * iceenv.num_mcu_y);
    printf("MCU dimension: %dx%d\n", iceenv.max_samp_x << 3, iceenv.max_samp_y << 3);
#endif
    
//    if (iceenv.scan_buffer[iceenv.buf_pos] == 0xFF && iceenv.scan_buffer[iceenv.buf_pos + 1] == 0x00)
//        skip_stuff_byte = 1;
    
    init_idct();
    
    for (iceenv.cur_mcu_x = iceenv.cur_mcu_y = 0;;)
    {
            //printf("Decoding MCU [%d,%d]\n", iceenv.cur_mcu_x, iceenv.cur_mcu_y);
            decode_mcu();
        
            iceenv.cur_mcu_x++;
            if (iceenv.cur_mcu_x == iceenv.num_mcu_x)
            {
                iceenv.cur_mcu_x = 0;
                iceenv.cur_mcu_y++;
                if (iceenv.cur_mcu_y == iceenv.num_mcu_y)
                    break;
            }
        
            if (!iceenv.restart_interval)
                continue;
            
            iceenv.rstcount--;
            if (!iceenv.rstcount)
            {
                int err = process_rst();
                if (err != ERR_OK)
                    return err;
            }
    }
    
    //if (iceenv.cur_byte_remaining > 0)
    iceenv.buf_pos++;
    
    return ERR_OK;
}

int upsample(void)
{
    int comp;
    for (comp = 0; comp < iceenv.sof0.num_components; comp++)
    {
        while (iceenv.components[comp].width < iceenv.sof0.width)
#ifndef USE_LANCZOS_UPSAMPLING
            upsampleBicubicH(&components[comp]);
#else
            upsampleLanczosH(&iceenv.components[comp]);
#endif
        
        while (iceenv.components[comp].height < iceenv.sof0.height)
#ifndef USE_LANCZOS_UPSAMPLING
            upsampleBicubicV(&components[comp]);
#else
            upsampleLanczosV(&iceenv.components[comp]);
#endif
    }
    
    return ERR_OK;
}

int create_image(void)
{
    // put image together
	iceenv.image = (byte*) malloc((iceenv.sof0.width * iceenv.sof0.height) * iceenv.sof0.num_components);
    
	if (iceenv.sof0.num_components == 3)
	{
		int x, y;
		byte *curImage = iceenv.image;
		byte *py = iceenv.components[0].pixels;
		byte *pcb = iceenv.components[1].pixels;
		byte *pcr = iceenv.components[2].pixels;
		for (y = 0; y <iceenv.sof0.height; y++)
		{
			for (x = 0; x < iceenv.sof0.width; x++)
			{
				register int cr = pcr[x] - 128;
				register int cb = pcb[x] - 128;

				// y must be multiplied by 128 because it DOES NOT receive a factor
				// during the conversion to RGB
				// since the other factors have been multiplied by 128,
				// y's factor (which is 1) must be multiplied by 128 as well
				register int y = py[x] << 7;

				// all conversion constants have been multiplied by 128
				*curImage++ = DESCALE8(y + 179 * cr);
				*curImage++ = DESCALE8(y - 44 * cb - 91 * cr);
				*curImage++ = DESCALE8(y + 227 * cb);
			}
			py += iceenv.components[0].stride;
			pcb += iceenv.components[1].stride;
			pcr += iceenv.components[2].stride;
		}
	}
	else
	if (iceenv.sof0.num_components == 1)
	{
		int y = 0;
		for (; y < iceenv.sof0.height; y++)
		{
			memcpy(iceenv.image + (y * iceenv.sof0.width), iceenv.components[0].pixels + (y * iceenv.components[0].stride), iceenv.sof0.width);
		}
	}

    return ERR_OK;
}

int process_segment(void)
{
    word marker;
    int err;
    
    marker = fetch_word();
    if (marker == 0xFFD9)
    {
		iceenv.eoi = 1;
#ifdef _JPEG_DEBUG
        printf("EOI detected! Done.\n");
#endif
        return ERR_OK;
    }
    
    iceenv.cur_segment_len = fetch_word();
    
    switch (marker)
    {
        case 0xFFE0:
            err = process_app0();
            break;
        case 0xFFDB:
            err = process_dqt();
            break;
        case 0xFFC0:
            err = process_sof0();
            break;
        case 0xFFC4:
            err = process_dht();
            break;
        case 0xFFDD:
            err = process_dri();
            break;
        case 0xFFDA:
            err = gen_huffman_tables();
            err = process_sos();
            err = decode_scan();
            err = upsample();
            err = create_image();
            break;
		case 0xFFC1:
		case 0xFFC2:
		case 0xFFC3:
		case 0xFFC5:
		case 0xFFC6:
		case 0xFFC7:
		case 0xFFC9:
		case 0xFFCA:
		case 0xFFCB:
		case 0xFFCD:
		case 0xFFCE:
		case 0xFFCF:
			err = ERR_NOT_BASELINE;
			break;
		default:
#ifdef _JPEG_DEBUG
            printf("Skipping unknown segment %X\n", marker & 0xFF);
#endif
            iceenv.buf_pos += iceenv.cur_segment_len - 2;
            err = ERR_OK;
            break;
    }
    
    return err;
}

void cleanup_dht(void)
{
    int i;
    for (i = 0; i < 2; i++)
    {
        if (iceenv.dc_dht[i])
        {
            free((void*)iceenv.dc_dht[i]->codes);
            free((void*)iceenv.dc_dht[i]);
        }
        if (iceenv.ac_dht[i])
        {
            free((void*)iceenv.ac_dht[i]->codes);
            free((void*)iceenv.ac_dht[i]);
        }
    }
}

void cleanup_huffman_tables(void)
{
    int i, j;
    for (i = 0; i < 2; i++)
    {
        if (iceenv.huff_dc[i])
        {
            for (j = 0; j < 0xFFFF; j++)
            {
                if (iceenv.huff_dc[i][j])
                    free((void*)iceenv.huff_dc[i][j]);
            }
            free((void*)iceenv.huff_dc[i]);
        }
        if (iceenv.huff_ac[i])
        {
            for (j = 0; j < 0xFFFF; j++)
            {
                if (iceenv.huff_ac[i][j])
                    free((void*)iceenv.huff_ac[i][j]);
            }
            free((void*)iceenv.huff_ac[i]);
        }
    }
}

void cleanup_qt_tables(void)
{
    int i;
    for (i = 0; i < 4; i++)
    {
        if (iceenv.qt_tables[i])
            free((void*)iceenv.qt_tables[i]);
    }
}

void cleanup(void)
{
    cleanup_qt_tables();
    cleanup_huffman_tables();
    
    free((void*)iceenv.components);
    free((void*)iceenv.buffer);
}