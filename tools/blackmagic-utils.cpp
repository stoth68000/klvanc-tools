
#include "blackmagic-utils.h"
#include <DeckLinkAPI.h>

const static struct blackmagic_format_s blackmagic_formats_table[] =
{
    { 0,                bmdModePAL,      1,    25, 0,  720,  288,  720,  576, 1920, "720x576i", },
    { 0,               bmdModeNTSC,   1001, 30000, 0,  720,  240,  720,  486, 1920, "720x480i",  },
    { 0,           bmdModeHD720p50,      1,    50, 1, 1280,  720, 1280,  720, 3456, "1280x720p50", },
    { 0,         bmdModeHD720p5994,   1001, 60000, 1, 1280,  720, 1280,  720, 3456, "1280x720p59.94", },
    { 0,           bmdModeHD720p60,      1,    60, 1, 1280,  720, 1280,  720, 3456, "1280x720p60", },
    { 0,          bmdModeHD1080i50,      1,    25, 0, 1920,  540, 1920, 1080, 5120, "1920x1080i25", },
    { 0,        bmdModeHD1080i5994,   1001, 30000, 0, 1920,  540, 1920, 1080, 5120, "1920x1080i29.97", },
    { 0,          bmdModeHD1080i6000,    1,    30, 0, 1920,  540, 1920, 1080, 5120, "1920x1080i30", },
    { 0,        bmdModeHD1080p2398,   1001, 24000, 1, 1920, 1080, 1920, 1080, 5120, "1920x1080p23.98", },
    { 0,          bmdModeHD1080p24,      1,    24, 1, 1920, 1080, 1920, 1080, 5120, "1920x1080p24", },
    { 0,          bmdModeHD1080p25,      1,    25, 1, 1920, 1080, 1920, 1080, 5120, "1920x1080p25", },
    { 0,        bmdModeHD1080p2997,   1001, 30000, 1, 1920, 1080, 1920, 1080, 5120, "1920x1080p29.97", },
    { 0,          bmdModeHD1080p30,      1,    30, 1, 1920, 1080, 1920, 1080, 5120, "1920x1080p30", },
    { 0,          bmdModeHD1080p50,      1,    50, 1, 1920, 1080, 1920, 1080, 5120, "1920x1080p50", },
    { 0,        bmdModeHD1080p5994,   1001, 60000, 1, 1920, 1080, 1920, 1080, 5120, "1920x1080p59.94", },
    { 0,        bmdModeHD1080p6000,      1,    60, 1, 1920, 1080, 1920, 1080, 5120, "1920x1080p60", },
#if BLACKMAGIC_DECKLINK_API_VERSION >= 0x0a0b0000 /* 10.11.0 */
/* These are also usable in 10.8.5 */
    /* 4K */
    { 0,          bmdMode4K2160p25,      1,    25, 1, 3840, 2160, 3840, 2160, 5120, "3840x2160p25", },
    { 0,        bmdMode4K2160p2997,   1001, 30000, 1, 3840, 2160, 3840, 2160, 5120, "3840x2160p29.97", },
    { 0,          bmdMode4K2160p50,      1,    50, 1, 3840, 2160, 3840, 2160, 5120, "3840x2160p50", },
    { 0,        bmdMode4K2160p5994,   1001, 60000, 1, 3840, 2160, 3840, 2160, 5120, "3840x2160p59.94", },
#endif
};

const struct blackmagic_format_s *blackmagic_getFormatByMode(uint32_t mode_id)
{
	const struct blackmagic_format_s *fmt;

	for (int i = 0; sizeof(blackmagic_formats_table) / sizeof(struct blackmagic_format_s); i++) {
		fmt = &blackmagic_formats_table[i];
		if (fmt->fmt == mode_id) {
			return fmt;
		}
	}

	return 0;
}

