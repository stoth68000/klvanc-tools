
/*
 Only supports V210 video and S32 audio.

 The order of operations for calls is always....
 bw_flash_avoffset_initialize()

 foreach incoming audio or video frame {
   if videoframe call bw_flash_avoffset_write_video_V210()
   if audioframe call bw_flash_avoffset_write_audio()
 }
*/

#define BW_FLASH_MAX_AUDIO_CHANNELS 16

struct bw_flash_avoffset_ctx_s
{
	int v_strideBytes;
	int v_width;
	int v_height;
	int v_interlaced;
	int currentVideoFrame_hasFlash;

	int a_strideBytes;
	int a_channels;
	uint32_t a_channelsActiveBitmask; /* 15:0 */

	struct
	{
		int enabled;
		uint64_t samplesSinceFlash;
		uint64_t activeOffsetSinceFlash;
		uint64_t offsetInLastSample;
	} a_channel_data[BW_FLASH_MAX_AUDIO_CHANNELS];

	uint64_t lastFlashAudioCounts[BW_FLASH_MAX_AUDIO_CHANNELS];
	uint64_t lastFlashAudioOffsets[BW_FLASH_MAX_AUDIO_CHANNELS];

	int reportLines;
};

static __inline__ int bw_flash_avoffset_initialize(struct bw_flash_avoffset_ctx_s *ctx,
	int videoStrideBytes, int videoWidth, int videoHeight, int videoInterlaced,
	int audioChannels, int audioStrideBytes, uint32_t a_channelsActiveBitmask)
{
	memset(ctx, 0, sizeof(*ctx));
	ctx->v_strideBytes = videoStrideBytes;
	ctx->v_width = videoWidth;
	ctx->v_height = videoHeight;
	ctx->v_interlaced = videoInterlaced;
	ctx->a_strideBytes = audioStrideBytes;
	ctx->a_channels = audioChannels;
	ctx->a_channelsActiveBitmask = a_channelsActiveBitmask;

	for (int i = 0; i < BW_FLASH_MAX_AUDIO_CHANNELS; i++) {
		if (ctx->a_channelsActiveBitmask & (1 << i)) {
			ctx->a_channel_data[i].enabled = 1;
		}
	}

	return 0;
};

static __inline__ int bw_flash_avoffset_write_video_V210(struct bw_flash_avoffset_ctx_s *ctx, const unsigned char *frame)
{
	/* Detect the flash and update this according. */
	uint32_t *p = (uint32_t *)frame;
	if ((*p & 0xffff) > 0x200) {
		ctx->currentVideoFrame_hasFlash = 1;
	} else {
		ctx->currentVideoFrame_hasFlash = 0;
	}
	//printf("ctx->currentVideoFrame_hasFlash = %d 0x%08x\n", ctx->currentVideoFrame_hasFlash, *p);

	return 0;
}

static __inline__ void bw_flash_avoffset__process_audio_offsets(struct bw_flash_avoffset_ctx_s *ctx)
{
	for (int c = 0; c < ctx->a_channels; c++) {
		if (ctx->a_channel_data[c].enabled) {
			ctx->lastFlashAudioCounts[c] = ctx->a_channel_data[c].samplesSinceFlash;
			ctx->lastFlashAudioOffsets[c] = ctx->a_channel_data[c].activeOffsetSinceFlash;
		}
	}
};

static __inline__ void bw_flash_avoffset__reset_audio_offsets(struct bw_flash_avoffset_ctx_s *ctx)
{
	for (int c = 0; c < ctx->a_channels; c++) {
		if (ctx->a_channel_data[c].enabled) {
			ctx->a_channel_data[c].samplesSinceFlash = 0;
			ctx->a_channel_data[c].activeOffsetSinceFlash = 0;
		}
	}
};

static __inline__ int bw_flash_avoffset_write_audio_s32(struct bw_flash_avoffset_ctx_s *ctx, const unsigned char *frame, int sampleFrameCount, int *resultsUpdated)
{
	if (ctx->currentVideoFrame_hasFlash) {
		*resultsUpdated = 1;
		bw_flash_avoffset__process_audio_offsets(ctx);
		bw_flash_avoffset__reset_audio_offsets(ctx);
	} else {
		*resultsUpdated = 0;
	}

	for (int c = 0; c < ctx->a_channels; c++) {
		ctx->a_channel_data[c].offsetInLastSample = 0;
	}

	int32_t *p = (int32_t *)frame;
	for (int f = 0; f < sampleFrameCount; f++) {
		for (int c = 0; c < ctx->a_channels; c++) {

			if (*p == 0) {
				ctx->a_channel_data[c].samplesSinceFlash++;
			}

			if (*p && ctx->a_channel_data[c].offsetInLastSample == 0) {
				ctx->a_channel_data[c].offsetInLastSample = f;
			}

			if ((*p > 0) && ctx->a_channel_data[c].activeOffsetSinceFlash == 0) {
				ctx->a_channel_data[c].activeOffsetSinceFlash = ctx->a_channel_data[c].samplesSinceFlash;
			}

			p++;
		}
	}

	return 0;
}

static __inline__ int bw_flash_avoffset_query_audio_counts(struct bw_flash_avoffset_ctx_s *ctx, uint64_t *arr, int arrayLength)
{
	if (arrayLength < BW_FLASH_MAX_AUDIO_CHANNELS)
		return -1;

	memcpy(arr, &ctx->lastFlashAudioCounts[0], BW_FLASH_MAX_AUDIO_CHANNELS * sizeof(uint64_t));
	//memcpy(arr, &ctx->lastFlashAudioOffsets[0], BW_FLASH_MAX_AUDIO_CHANNELS * sizeof(uint64_t));
	
	return 0;
};

static __inline__ void bw_flash_avoffset_query_report_to_fd(struct bw_flash_avoffset_ctx_s *ctx, int fd)
{
	uint64_t counts[BW_FLASH_MAX_AUDIO_CHANNELS];
	bw_flash_avoffset_query_audio_counts(ctx, &counts[0], BW_FLASH_MAX_AUDIO_CHANNELS);

	if ((ctx->reportLines++ % 20) == 0) {
		dprintf(fd, "Ch#%5d %8d %8d %8d %8d %8d %8d %8d %8d %8d %8d %8d %8d %8d %8d %8d\n",
			0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
	}
	for (int i = 0; i < 16; i++) {
		if (counts[i]) {
			dprintf(fd, "%08d ", counts[i]);
		} else {
			dprintf(fd, "%8s ", "");
		}
	}
	dprintf(fd, "\n");
}
