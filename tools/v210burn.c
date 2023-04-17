#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include "font8x8_basic.h"

static void compute_colorbar_10bit_array(const uint32_t uyvy, uint8_t *bar10)
{
	uint8_t *bar8 = (uint8_t *)&uyvy;
	uint16_t cb, y0, cr;

	cb = bar8[0] << 2;
	y0 = bar8[1] << 2;
	cr = bar8[2] << 2;

	bar10[0] = cb & 0xff;
	bar10[1] = (cb >> 8) | ((y0 & 0x3f) << 2);
	bar10[2] = (y0 >> 6) | ((cr & 0x0f) << 4);
	bar10[3] = (cr >> 4);

	bar10[4] = y0 & 0xff;
	bar10[5] = (y0 >> 8) | ((cb & 0x3f) << 2);
	bar10[6] = (cb >> 6) | ((y0 & 0x0f) << 4);
	bar10[7] = (y0 >> 4);

	bar10[8] = cr & 0xff;
	bar10[9] = (cr >> 8) | ((y0 & 0x3f) << 2);
	bar10[10] = (y0 >> 6) | ((cb & 0x0f) << 4);
	bar10[11] = (cb >> 4);

	bar10[12] = y0 & 0xff;
	bar10[13] = (y0 >> 8) | ((cr & 0x3f) << 2);
	bar10[14] = (cr >> 6) | ((y0 & 0x0f) << 4);
	bar10[15] = (y0 >> 4);
}

static int v210_burn_char(unsigned char *frame,
			  unsigned int stride, uint8_t letter, unsigned int x, unsigned int y)
{
	uint8_t line;
	uint8_t bar10_fg[16];
	uint8_t bar10_bg[16];
	int plotctrl = 4;
	int plotwidth = 4 * plotctrl;
	int plotheight = 8 * plotctrl;

	unsigned char fg[2];
	unsigned char bg[2];
	unsigned char *ptr;

	ptr = frame + (x * (plotwidth * 2 * 2)) + (y * plotheight * stride);

	/* Black */
	bg[0] = 0x80;
	bg[1] = 0x00;

	/* Slight off white, closer to grey */
	fg[0] = 0x80;
	fg[1] = 0x90;

	if (letter > 0x9f)
		return -1;

	compute_colorbar_10bit_array(fg[0] | (fg[1] << 8) |
				     (fg[0] << 16) | (fg[1] << 24),
				     &bar10_fg[0]);

	compute_colorbar_10bit_array(bg[0] | (bg[1] << 8) |
				     (bg[0] << 16) | (bg[1] << 24),
				     &bar10_bg[0]);

	for (int i = 0; i < 8; i++) {
		int k = 0;
		while (k++ < 4) {
			line = font8x8_basic[letter][ i ];
			for (int j = 0; j < 4; j++) {
				/* Hack which takes advantage of the fact that
				   both the FG and BG have the same chroma */
				for (int c=0; c < 2; c++) {
					if (line & 0x01) {
						for (int n = 0; n < 8; n++)
							*(ptr + n) = bar10_fg[n];
						if (plotctrl == 8) {
							for (int n = 0; n < 8; n++)
								*(ptr + 8 + n) = bar10_fg[n];
						}
					} else {
						for (int n = 0; n < 8; n++)
							*(ptr + n) = bar10_bg[n];
						if (plotctrl == 8) {
							for (int n = 0; n < 8; n++)
								*(ptr + 8 + n) = bar10_bg[n];
						}
					}
					line >>= 1;
					ptr += plotctrl * 2;
				}
			}
			ptr += ((stride) - (plotctrl * 4 * 4));
		}
	}
	return 0;
}

int v210_burn(unsigned char *frame, unsigned int width, unsigned int height,
	      unsigned int stride, const char *s, unsigned int x, unsigned int y)
{
	if (!s || (strlen(s) > 128))
		return -1;

	for (unsigned int i = 0; i < strlen(s); i++)
		v210_burn_char(frame, stride, *(s + i), x + i, y);

	return 0;
}
