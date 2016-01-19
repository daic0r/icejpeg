#ifndef _encode_h
#define _encode_h

// Bit precision for integer calculations
#define PRECISION 8

int icejpeg_encode_init(const char *filename, unsigned char *image, int width, int height, int y_samp, int cb_samp, int cr_samp);
void icejpeg_setquality(unsigned char quality);
void icejpeg_set_restart_markers(int userst);
int icejpeg_write(void);
void icejpeg_encode_cleanup();

#endif