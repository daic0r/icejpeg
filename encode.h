#ifndef _encode_h
#define _encode_h

// Bit precision for integer calculations
#define PRECISION 8

int icejpeg_encode_init(const char *filename, unsigned char *image, int width, int height, int y_samp, int cb_samp, int cr_samp);
int icejpeg_write(void);
void icejpeg_encode_cleanup();

#endif