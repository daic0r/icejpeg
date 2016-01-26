//
//  main.c
//  icejpeg
//
//  Created by Matthias Gruen on 11/01/16.
//  Copyright © 2016 Matthias Gruen. All rights reserved.
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common.h"
#include "decode.h"
#include "encode.h"

static void generate_constants()
{
    int y_r = rnd(0.299f * (1 << PRECISION));
    int y_g = rnd(0.587f * (1 << PRECISION));
    int y_b = rnd(0.114f * (1 << PRECISION));
    
    int cb_r = rnd(-0.168736f * (1 << PRECISION));
    int cb_g = rnd(-0.331264f * (1 << PRECISION));
    int cb_b = rnd(0.5f * (1 << PRECISION));
    
    int cr_r = rnd(0.5f * (1 << PRECISION));
    int cr_g = rnd(-0.418688f * (1 << PRECISION));
    int cr_b = rnd(-0.081312f * (1 << PRECISION));
    
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

int main(int argc, const char** argv)
{
  
    const char * basename = "example";
    
    char jpegfile[40];
    strcpy(jpegfile, basename);
    
    int err = icejpeg_decode_init(strcat(jpegfile, ".jpg"));
    
    printf("Err = %d\n", err);
    
    if (err == -1)
    {
        printf("File couldn't be opened!\n");
        return err;
    }
    
    unsigned char *my_image = 0;
    int img_width, img_height, img_components;
    
    err = icejpeg_read(&my_image, &img_width, &img_height, &img_components);
    
    if (err != ERR_OK)
    {
        printf("Error parsing JPEG file!\n");
        if (err == ERR_NOT_BASELINE)
        {
            printf("Reason: Only baseline JPEGs are suppported!\n");
        }
        return err;
    }

	struct jpeg_encoder_settings settings;
	settings.width = img_width;
	settings.height = img_height;
	settings.num_components = 3;
	settings.quality = 10;
	settings.use_rst_markers = 0;
	settings.sampling_factors[0].sx = 2;
    settings.sampling_factors[0].sy = 2;
	settings.sampling_factors[1].sx = 1;
	settings.sampling_factors[1].sy = 1;
	settings.sampling_factors[2].sx = 1;
	settings.sampling_factors[2].sy = 1;

	icejpeg_encode_init("out.jpg", my_image, &settings);
	err = icejpeg_write();
    
    struct jpeg_encoder_stats *stats;
    icejpeg_get_stats(&stats);
    printf("Bits per pixel: %3.2f\n", stats->bits_per_pixel);
    printf("Compression ratio: %3.2f\n", stats->compression_ratio);
    
	icejpeg_encode_cleanup();

     char outfile[40];
     strcpy(outfile, basename);
    
     
//     FILE *f = fopen(strcat(outfile, ".ppm"), "wb");
//     if (!f) {
//         printf("Error opening the output file.\n");
//         return 1;
//     }
//     fprintf(f, "P%d\n%d %d\n255\n", img_components == 3 ? 6 : 5 , img_width, img_height);
//     fwrite(my_image, 1, (img_width * img_height) * img_components, f);
//     fclose(f);
    
     free((void*)my_image);
    
    icejpeg_cleanup();
    
    printf("Done.");
    
    return 0;
    
}