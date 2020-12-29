
/*
 * Generic helper functions specific to blackmagic decklink hardware APIs.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

struct blackmagic_format_s
{
    uint32_t reserved;
    uint32_t fmt;
    int timebase_num;
    int timebase_den;
    int is_progressive;
    int visible_width;
    int visible_height;
    int callback_width;     /* Width, height, stride provided during the frame callback. */
    int callback_height;    /* Width, height, stride provided during the frame callback. */
    int callback_stride;    /* Width, height, stride provided during the frame callback. */
    const char *ascii_name;
};

/* For a given mode_id (blackmagic SDK type BMDDisplayMode), lookup some translation information. */
const struct blackmagic_format_s *blackmagic_getFormatByMode(uint32_t mode_id);

#ifdef __cplusplus
};
#endif
