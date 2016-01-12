//
//  decode.h
//  icejpeg
//
//  Created by Matthias Gruen on 11/01/16.
//  Copyright © 2016 Matthias Gruen. All rights reserved.
//

#ifndef decode_h
#define decode_h

int icejpeg_decode_init(const char* filename);
int icejpeg_read(unsigned char **buffer, int *width, int *height, int *num_components);
void icejpeg_cleanup(void);


#endif /* decode_h */
