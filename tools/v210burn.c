#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include "font8x8_basic.h"

/* must be a multiple of six (represents pixel width of each box drawn ) */
#define V210_BOX_HEIGHT_SD 18
#define V210_BOX_HEIGHT_HD 30

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
	int plotwidth, plotheight;
	int plotctrl = stride < 3456 ? 4 : 8;
	plotwidth = 4 * plotctrl;
	plotheight = 8 * plotctrl;

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
			for (int m = 0; m < (plotctrl / 4); m++) {
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

#define  y_white 0x3ff
#define  y_black 0x000
#define cr_white 0x200
#define cb_white 0x200

/* Six pixels */
uint32_t V210_white[] = {
	 cr_white << 20 |  y_white << 10 | cb_white,
	  y_white << 20 | cb_white << 10 |  y_white,
	 cb_white << 20 |  y_white << 10 | cr_white,
	  y_white << 20 | cr_white << 10 |  y_white,
};

uint32_t V210_black[] = {
	 cr_white << 20 |  y_black << 10 | cb_white,
	  y_black << 20 | cb_white << 10 |  y_black,
	 cb_white << 20 |  y_black << 10 | cr_white,
	  y_black << 20 | cr_white << 10 |  y_black,
};


/* KL paint 6 pixels in a single point */
__inline__ void V210_draw_6_pixels(uint32_t *addr, uint32_t *coloring, int boxsize)
{
	for (int i = 0; i < (boxsize / 6); i++) {
		addr[0] = coloring[0];
		addr[1] = coloring[1];
		addr[2] = coloring[2];
		addr[3] = coloring[3];
		addr += 4;
	}
}

void V210_draw_box(uint32_t *frame_addr, uint32_t stride, int color, int interlaced)
{
	int boxsize = stride < 3456 ? V210_BOX_HEIGHT_SD : V210_BOX_HEIGHT_HD;
	uint32_t *coloring;
	if (color == 1)
		coloring = V210_white;
	else
		coloring = V210_black;

	int interleaved = interlaced ? 2 : 1;
	interleaved = 1;
	for (uint32_t l = 0; l < boxsize; l++) {
		uint32_t *addr = frame_addr + ((l * interleaved) * (stride / 4));
		V210_draw_6_pixels(addr, coloring, boxsize);
	}
}

__inline__ void V210_draw_box_at(uint32_t *frame_addr, uint32_t stride, int color, int x, int y, int interlaced)
{
	uint32_t *addr = frame_addr + (y * (stride / 4));
	addr += ((x / 6) * 4);
	V210_draw_box(addr, stride, color, interlaced);
}

void V210_write_32bit_value(void *frame_bytes, uint32_t stride, uint32_t value, uint32_t lineNr, int interlaced)
{
	int boxsize = stride < 3456 ? V210_BOX_HEIGHT_SD : V210_BOX_HEIGHT_HD;
	for (int p = 31, sh = 0; p >= 0; p--, sh++) {
		V210_draw_box_at(((uint32_t *)frame_bytes), stride,
			(value & (1 << sh)) == (uint32_t)(1 << sh), p * boxsize, lineNr, interlaced);
	}
}

uint32_t V210_read_32bit_value(void *frame_bytes, uint32_t stride, uint32_t lineNr, double scalefactor)
{
	int boxsize = stride < 3456 ? V210_BOX_HEIGHT_SD : V210_BOX_HEIGHT_HD;
	double pixheight = boxsize * scalefactor;
	double newlinenr = lineNr * scalefactor;

	int xpos = 0;
	uint32_t bits = 0;
	for (int i = 0; i < 32; i++) {
		xpos = (i * pixheight) + (pixheight / 2);
		/* Sample the pixel two lines deeper than the initial line, and eight pixels in from the left */
		uint32_t *addr = ((uint32_t *)frame_bytes) + (((int)newlinenr + 2) * (stride / 4));
		addr += ((xpos / 6) * 4);

		bits <<= 1;

		/* Sample the pixel.... Compressor will decimate, we'll need a luma threshold for production. */
		if ((addr[1] & 0x3ff) > 0x080)
			bits |= 1;
	}
#if LOCAL_DEBUG
	printf("%s(%p) = 0x%08x\n", __func__, frame_bytes, bits);
#endif
	return bits;
}
