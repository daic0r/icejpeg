#ifndef _encode_h
#define _encode_h

// Bit precision for integer calculations
#define PRECISION 8

struct jpeg_encoder_settings {
	int width, height;
	int num_components;
	unsigned char quality;
	int use_rst_markers;
	struct __factors {
		int sx, sy;
	} sampling_factors[3];
};

int icejpeg_encode_init(const char *filename, unsigned char *image, struct jpeg_encoder_settings *settings);
void icejpeg_setquality(unsigned char quality);
void icejpeg_set_restart_markers(int userst);
int icejpeg_write(void);
void icejpeg_encode_cleanup();

#endif