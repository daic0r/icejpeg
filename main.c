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
        if (err == ERR_PROGRESSIVE)
        {
            printf("Reason: Progressive JPEGs not supported!\n");
        }
        return err;
    }

	icejpeg_encode_init("out.jpg", my_image, img_width, img_height, 2, 1, 1);
	err = icejpeg_write();
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