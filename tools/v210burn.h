/* Minimalist implementation to burn text into v210 video */

#ifndef v210burn_h
#define v210burn_h

#ifdef __cplusplus
extern "C" {
#endif

int v210_burn(unsigned char *frame, unsigned int width, unsigned int height,
	      unsigned int stride, const char *s, unsigned int x, unsigned int y);

#ifdef __cplusplus
};
#endif

#endif
