/* Minimalist implementation to burn text into v210 video */

#ifndef v210burn_h
#define v210burn_h

#ifdef __cplusplus
extern "C" {
#endif

int v210_burn(unsigned char *frame, unsigned int width, unsigned int height,
	      unsigned int stride, const char *s, unsigned int x, unsigned int y);

void V210_write_32bit_value(void *frame_bytes, uint32_t stride, uint32_t value, uint32_t lineNr, int interlaced);
uint32_t V210_read_32bit_value(void *frame_bytes, uint32_t stride, uint32_t lineNr, double scalefactor);

#ifdef __cplusplus
};
#endif

#endif
