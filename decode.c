// icejpeg.c

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
#define max(a,b) (((a) > (b)) ? (a) : (b))
#define min(a,b) (((a) < (b)) ? (a) : (b))
#define MAX_DC_TABLES 4
#define MAX_AC_TABLES 4

#define USE_LANCZOS_UPSAMPLING


FILE *file; // Our jpeg file
byte *file_buf;
int buf_pos = 0;
struct stat st; // file stats
word cur_segment_len;

byte cur_byte_remaining = 8;

byte max_samp_x, max_samp_y;
int eoi = 0;
int block[64];
int mcu_width, mcu_height;
int num_mcu_x, num_mcu_y;
int cur_mcu_x, cur_mcu_y;
int cur_du_x, cur_du_y;
int restart_interval;
byte next_rst_marker = 0;
int rstcount = 0;
byte *image;
struct jpeg_component *components;

#pragma pack(push)
#pragma pack(1)

const byte jpeg_zigzag[] =
{
    0, 1, 8, 16, 9, 2, 3, 10,
    17, 24, 32, 25, 18, 11, 4, 5,
    12, 19, 26, 33, 40, 48, 41, 34,
    27, 20, 13, 6, 7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36,
    29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46,
    53, 60, 61, 54, 47, 55, 62, 63
};

typedef byte* jpeg_dqttable;
jpeg_dqttable qt_tables[4];

struct jpeg_app0
{
    char strjfif[5];
    byte maj_revision, min_revision;
    byte xy_dens_unit;
    word xdensity, ydensity;
    byte thumb_width, thumb_height;
} app0;

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
} sof0;

struct jpeg_dht
{
    byte num_codes[16];
    byte *codes;
} *dc_dht[MAX_DC_TABLES], *ac_dht[MAX_AC_TABLES];

struct jpeg_huffman_code
{
    word code;
    byte length;
};

typedef struct jpeg_huffman_code** jpeg_huffman_table;	// array of pointers to a huffman code

jpeg_huffman_table huff_dc[MAX_DC_TABLES];
jpeg_huffman_table huff_ac[MAX_AC_TABLES];



#pragma pack(pop)

void cleanup(void);
int process_segment(void);
void cleanup_dht(void);

int icejpeg_decode_init(const char* filename)
{
    file = fopen(filename, "rb");
    if (!file)
        return ERR_OPENFILE_FAILED;
    
    fstat(fileno(file), &st);
    
    file_buf = (byte *)malloc(sizeof(byte) * st.st_size);
    fread((void*)file_buf, sizeof(byte), st.st_size, file);
    
#ifdef _JPEG_DEBUG
    printf("%lld bytes read.\n", st.st_size);
    printf("%d\n", ferror(file));
#endif
    
    fclose(file);
    
    if (file_buf[buf_pos++] != 0xFF || file_buf[buf_pos++] != 0xD8)
    {
        cleanup();
        return ERR_NO_JPEG;
    }
    
    int i;
    for (i = 0; i < MAX_DC_TABLES; i++)
    {
        dc_dht[i] = 0;
        huff_dc[i] = 0;
    }
    for (i = 0; i < MAX_AC_TABLES; i++)
    {
        ac_dht[i] = 0;
        huff_ac[i] = 0;
    }
    
    for (i = 0; i < 4; i++)
    {
        qt_tables[i] = 0;
    }
    
    restart_interval = 0;
    
    return ERR_OK;
}

int icejpeg_read(unsigned char **buffer, int *width, int *height, int *num_components)
{
    int err;
    while (!eoi)
    {
        err = process_segment();
        if (err != ERR_OK)
            return err;
    }
    
    *buffer = image;
    *width = sof0.width;
    *height = sof0.height;
    *num_components = sof0.num_components;
    
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
    temp = file_buf[buf_pos++];
    temp <<= 8;
    temp |= file_buf[buf_pos++];
    return temp;
}

word flip_byte_order(word inword)
{
    return ((inword & 0xFF) << 8) | ((inword & 0xFF00) >> 8);
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
            cur_src_table = dc_dht[i];
        }
        else
        {
            cur_src_table = ac_dht[i - MAX_DC_TABLES];
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
            huff_dc[i] = cur_dst_table;
        }
        else
        {
            huff_ac[i - MAX_DC_TABLES] = cur_dst_table;
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
        bits_from_cur_byte = min(cur_byte_remaining, num_bits);
        byte mask_shift = cur_byte_remaining - bits_from_cur_byte;
        
        mask = (1 << bits_from_cur_byte) - 1;
        //mask <<= mask_shift;
        result <<= bits_from_cur_byte;
        result |= (file_buf[buf_pos] >> mask_shift) & mask;
        
        cur_byte_remaining = max(0, cur_byte_remaining - num_bits);
        num_bits -= bits_from_cur_byte;
        
        if (!cur_byte_remaining && num_bits)
        {
            if (file_buf[buf_pos] == 0xFF)
            {
                switch (file_buf[buf_pos + 1])
                {
                    case 0x00:
                        buf_pos++;
                        break;
                    default:
                        printf("ERROR: Marker detected in bit stream!\n");
                        getc(stdin);
                        break;
                }
            }
            cur_byte_remaining = 8;
            buf_pos++;
            // Skip stuff bytes
//            if (skip_stuff_byte)
//            {
//                buf_pos++;
//                skip_stuff_byte = 0;
//            }
//            if (file_buf[buf_pos] == 0xFF)
//            {
//                switch (file_buf[buf_pos + 1])
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
    memcpy((void*)&app0, (void*) (file_buf + buf_pos), sizeof(struct jpeg_app0));
    
    if (strcmp(app0.strjfif, "JFIF"))
        return ERR_INVALID_JFIF_STRING;
    
    if (app0.maj_revision != 1)
        return ERR_INVALID_MAJOR_REV;
    
    buf_pos += cur_segment_len - 2;
    
    return ERR_OK;
}

int process_dqt(void)
{
    int bytes_read = 2;
    
    while (bytes_read < cur_segment_len)
    {
        byte info = file_buf[buf_pos++];
        // 16bit?
        if (info & 0xF0)
            return ERR_16BIT_DQT_NOT_SUPPORTED;
        
        qt_tables[info & 0xF] = (byte *)malloc(64);
        memcpy((void*)qt_tables[info & 0xF], (void*)(file_buf + buf_pos), 64);
        buf_pos += 64;
        bytes_read += 64 + 1;	// including the info byte above
    }
    
    return ERR_OK;
}

int process_sof0(void)
{
    max_samp_y = max_samp_x = 0;
    
    memcpy((void*)&sof0, (void*)(file_buf + buf_pos), sizeof(struct jpeg_sof0));
    buf_pos += sizeof(struct jpeg_sof0);
    
    if (sof0.num_components != 1 && sof0.num_components != 3)
        return ERR_INVALID_NUMBER_OF_COMP;
    
    sof0.width = flip_byte_order(sof0.width);
    sof0.height = flip_byte_order(sof0.height);
    
    struct jpeg_sof0_component_info comp_info[3];
    //comp_info[0] = comp_info[1] = comp_info[2] = 0;
    
    int i;
    // 	for (i = 0; i < sof0.num_components; i++)
    // 	{
    // 		comp_info[i] = (struct jpeg_sof0_component_info*)malloc(sizeof(struct jpeg_sof0_component_info));
    // 	}
    
    components = (struct jpeg_component*) malloc(sizeof(struct jpeg_component) * sof0.num_components);
    
    for (i = 0; i < sof0.num_components; i++)
    {
        memcpy((void*)&comp_info[i], (void*)(file_buf + buf_pos), sizeof(struct jpeg_sof0_component_info));
        buf_pos += sizeof(struct jpeg_sof0_component_info);
        
        components[i].qt_table = comp_info[i].qt_table;
        components[i].sx = (comp_info[i].sampling_factors & 0xF0) >> 4;
        components[i].sy = comp_info[i].sampling_factors & 0xF;
        components[i].prev_dc = 0;
        
        // Update maximum sampling factors
        if (components[i].sx > max_samp_x)
            max_samp_x = components[i].sx;
        if (components[i].sy > max_samp_y)
            max_samp_y = components[i].sy;
        
    }
    
    mcu_width = max_samp_x << 3;
    mcu_height = max_samp_y << 3;
    num_mcu_x = (sof0.width + mcu_width - 1) / mcu_width;
    num_mcu_y = (sof0.height + mcu_height - 1) / mcu_height;
    
    for (i = 0; i < sof0.num_components; i++)
    {
        components[i].width = (sof0.width * components[i].sx + max_samp_x - 1) / max_samp_x;
        components[i].height = (sof0.height * components[i].sy + max_samp_y - 1) / max_samp_y;
        components[i].stride = num_mcu_x * (components[i].sx << 3);
        components[i].pixels = (byte*)malloc(components[i].stride * (num_mcu_y * (components[i].sy << 3)) * sizeof(byte));
    }
    
#ifdef _JPEG_DEBUG
    printf("Hmax = %d, Vmax = %d\n", max_samp_x, max_samp_y);
#endif
    
    return ERR_OK;
}

int process_dht(void)
{
    int bytes_read = 2;
    
    while (bytes_read < cur_segment_len)
    {
        byte info = file_buf[buf_pos++];
        byte type_table = info & 0x10;
        struct jpeg_dht *cur_table;
        
        if (!type_table)
            //cur_table = dc_dht;
        {
            dc_dht[info & 0xF] = (struct jpeg_dht*)malloc(sizeof(struct jpeg_dht));
            cur_table = dc_dht[info & 0xF];
        }
        else
        {
            //	cur_table = ac_dht;
            ac_dht[info & 0xF] = (struct jpeg_dht*)malloc(sizeof(struct jpeg_dht));
            cur_table = ac_dht[info & 0xF];
        }
        
        //cur_table[info & 0xF] = (struct jpeg_dht*)malloc(sizeof(struct jpeg_dht));
        memcpy((void*)cur_table->num_codes, (void*)(file_buf + buf_pos), 16);
        buf_pos += 16;
        bytes_read += 16 + 1;
        
        int num_codes = 0;
        int i = 0;
        for (i = 0; i < 16; i++)
        {
            num_codes += cur_table->num_codes[i];
        }
        cur_table->codes = (byte *)malloc(num_codes);
        memcpy((void*)cur_table->codes, (void*)(file_buf + buf_pos), num_codes);
        
#ifdef _JPEG_DEBUG
        for (i = 0; i < num_codes; i++)
        {
            printf("%X ", cur_table->codes[i]);
        }
        printf("\n");
#endif
        
        buf_pos += num_codes;
        bytes_read += num_codes;
    }
    
    return ERR_OK;
}

int process_sos(void)
{
    byte num_components = file_buf[buf_pos++];
    if (cur_segment_len != 6 + 2 * num_components)
        return ERR_INVALID_SEGMENT_SIZE;
    
    if (!components)
        return ERR_SOF0_MISSING;
    
    int i = 0;
    for (i = 0; i < num_components; i++)
    {
        byte id = file_buf[buf_pos++];
        components[id-1].id_dht = file_buf[buf_pos++];
    }
    
    // Ignore the following 3 bytes
    buf_pos += 3;
    
    return ERR_OK;
}

int process_dri(void)
{
    if (cur_segment_len != 4)
        return ERR_INVALID_SEGMENT_SIZE;
    
    restart_interval = fetch_word();
    rstcount = restart_interval;
    
    return ERR_OK;
}

// Decode a single DU within an MCU
int decode_du(byte id_component)
{
    word bit_string = 0;
    byte cur_code = 0;
    
    memset(block, 0, sizeof(int) * 64);
    
    jpeg_huffman_table cur_table = huff_dc[(components[id_component].id_dht & 0xF0) >> 4];
    
    cur_code = get_next_code(cur_table);
    
#ifdef _JPEG_DEBUG
    printf("Code found: %X\n", cur_code);
#endif
    
    bit_string = fetch_bits(cur_code);
    
#ifdef _JPEG_DEBUG
    printf("Bits fetched: %d\n", bit_string);
#endif
    
    short value = get_signed_short(bit_string, cur_code);
    
    components[id_component].prev_dc += value;
    //mcu->dus[id_component][(y*samp_x) + x][0] = cur_mcu > 0 ? (mcus[cur_mcu - 1].dus[id_component][(y*samp_x) + x][0] + dc_value) : dc_value;
    block[0] = components[id_component].prev_dc;
    
#ifdef _JPEG_DEBUG
    printf("DC value: %d, absolute value: %d\n", block[0], value);
#endif
    
    // Dequantize DC value
    block[0] *= qt_tables[components[id_component].qt_table][0];
    
    // Switch to AC table
    cur_table = huff_ac[components[id_component].id_dht & 0xF];
    
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
        block_index += (cur_code & 0xF0) >> 4;
        if (block_index > 63)
            break;
        
        bit_string = fetch_bits(cur_code & 0xF);
        
#ifdef _JPEG_DEBUG
        printf("Fetched %d bits\n", cur_code & 0xF);
#endif
        
        value = get_signed_short(bit_string, cur_code & 0xF);
        
#ifdef _JPEG_DEBUG
        printf("AC value: %d\n", value);
#endif
        
        byte actual_index = jpeg_zigzag[block_index];
        
        
        // Dequantize and unzigzag at the same time
        block[actual_index] = value * qt_tables[components[id_component].qt_table][block_index];
        block_index++;
        
#ifdef _JPEG_DEBUG
        printf("Dequantized AC value: %d\n", block[actual_index]);
#endif
    }
    
    /****************************************************/
    /* Perform IDCT                                     */
    /****************************************************/
    byte rowscols;
    for (rowscols = 0; rowscols < 8; rowscols++)
    {
        idctrow(&block[8 * rowscols]);
    }
    for (rowscols = 0; rowscols < 8; rowscols++)
    {
        int targetPos = ((cur_mcu_y * (components[id_component].sy << 3) + (cur_du_y << 3)) * components[id_component].stride) + (cur_mcu_x * (components[id_component].sx << 3) + (cur_du_x << 3));
        idctcol(&block[rowscols], &components[id_component].pixels[targetPos + rowscols], components[id_component].stride);
    }
    
    return ERR_OK;
}

int decode_mcu(void)
{
    int comp;
    
    // Iterate over components (Y, Cb, Cr)
    for (comp = 0; comp < sof0.num_components; comp++)
    {
        // Iterate over sampling factors
        for (cur_du_y = 0; cur_du_y < components[comp].sy; cur_du_y++)
        {
            for (cur_du_x = 0; cur_du_x < components[comp].sx; cur_du_x++)
            {
                decode_du(comp);
            }
        }
    }
    
    return ERR_OK;
}

int process_rst(void)
{
    buf_pos++;
    cur_byte_remaining = 8;
    
    if (file_buf[buf_pos++] != 0xFF || (file_buf[buf_pos++] & 0xF) != next_rst_marker)
    {
        return ERR_INVALID_RST_MARKER;
    }
    
    next_rst_marker = (next_rst_marker + 1) & 7;
    
    rstcount = restart_interval;
    
    int i = 0;
    for (;i<sof0.num_components;i++)
        components[i].prev_dc = 0;
    
    return ERR_OK;
}

int decode_scan(void)
{
#ifdef _JPEG_DEBUG
    printf("%d MCUs in total.\n", num_mcu_x * num_mcu_y);
    printf("MCU dimension: %dx%d\n", max_samp_x << 3, max_samp_y << 3);
#endif
    
//    if (file_buf[buf_pos] == 0xFF && file_buf[buf_pos + 1] == 0x00)
//        skip_stuff_byte = 1;
    
    init_idct();
    
    for (cur_mcu_x = cur_mcu_y = 0;;)
    {
            //printf("Decoding MCU [%d,%d]\n", cur_mcu_x, cur_mcu_y);
            decode_mcu();
        
            cur_mcu_x++;
            if (cur_mcu_x == num_mcu_x)
            {
                cur_mcu_x = 0;
                cur_mcu_y++;
                if (cur_mcu_y == num_mcu_y)
                    break;
            }
        
            if (!restart_interval)
                continue;
            
            rstcount--;
            if (!rstcount)
            {
                int err = process_rst();
                if (err != ERR_OK)
                    return err;
            }
    }
    
    //if (cur_byte_remaining > 0)
    buf_pos++;
    
    return ERR_OK;
}

int upsample(void)
{
    int comp;
    for (comp = 0; comp < sof0.num_components; comp++)
    {
        while (components[comp].width < sof0.width)
#ifndef USE_LANCZOS_UPSAMPLING
            upsampleBicubicH(&components[comp]);
#else
            upsampleLanczosH(&components[comp]);
#endif
        
        while (components[comp].height < sof0.height)
#ifndef USE_LANCZOS_UPSAMPLING
            upsampleBicubicV(&components[comp]);
#else
            upsampleLanczosV(&components[comp]);
#endif
    }
    
    return ERR_OK;
}

int create_image(void)
{
    // put image together
    image = (byte*) malloc((sof0.width * sof0.height) * sof0.num_components);
    
	if (sof0.num_components == 3)
	{
		int x, y;
		byte *curImage = image;
		byte *py = components[0].pixels;
		byte *pcb = components[1].pixels;
		byte *pcr = components[2].pixels;
		for (y = 0; y < sof0.height; y++)
		{
			for (x = 0; x < sof0.width; x++)
			{
				register int cr = pcr[x] - 128;
				register int cb = pcb[x] - 128;
				register int y = py[x] << 7;

				// all conversion constants have been multiplied by 128
				*curImage++ = CF(y + 179 * cr);
				*curImage++ = CF(y - 44 * cb - 91 * cr);
				*curImage++ = CF(y + 227 * cb);
			}
			py += components[0].stride;
			pcb += components[1].stride;
			pcr += components[2].stride;
		}
	}
	else
	if (sof0.num_components == 1)
	{
		int y = 0;
		for (; y < sof0.height; y++)
		{
			memcpy(image + (y * sof0.width), components[0].pixels + (y * components[0].stride), sof0.width);
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
        eoi = 1;
#ifdef _JPEG_DEBUG
        printf("EOI detected! Done.\n");
#endif
        return ERR_OK;
    }
    
    cur_segment_len = fetch_word();
    
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
			err = ERR_PROGRESSIVE;
			break;
		default:
            printf("Skipping unknown segment %X\n", marker & 0xFF);
            buf_pos += cur_segment_len - 2;
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
        if (dc_dht[i])
        {
            free((void*)dc_dht[i]->codes);
            free((void*)dc_dht[i]);
        }
        if (ac_dht[i])
        {
            free((void*)ac_dht[i]->codes);
            free((void*)ac_dht[i]);
        }
    }
}

void cleanup_huffman_tables(void)
{
    int i, j;
    for (i = 0; i < 2; i++)
    {
        if (huff_dc[i])
        {
            for (j = 0; j < 0xFFFF; j++)
            {
                if (huff_dc[i][j])
                    free((void*)huff_dc[i][j]);
            }
            free((void*)huff_dc[i]);
        }
        if (huff_ac[i])
        {
            for (j = 0; j < 0xFFFF; j++)
            {
                if (huff_ac[i][j])
                    free((void*)huff_ac[i][j]);
            }
            free((void*)huff_ac[i]);
        }
    }
}

void cleanup_qt_tables(void)
{
    int i;
    for (i = 0; i < 4; i++)
    {
        if (qt_tables[i])
            free((void*)qt_tables[i]);
    }
}

void cleanup(void)
{
    cleanup_qt_tables();
    cleanup_huffman_tables();
    
    free((void*)components);
    free((void*)file_buf);
}