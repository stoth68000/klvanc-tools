
#ifndef HIRES_AV_DEBUG_H
#define HIRES_AV_DEBUG_H

/* Goal is to keep this as a single header, hghly portable, so that it can be ingested
 * into video applications quickly, to measure audio and video frame arrival times,
 * vs expected arrival times.
 *
 * To use this, create a long running static allocation of the core context and initialize it:
 *   struct hires_av_ctx_s havctx;
 *   hires_av_init(&havctx, 60000.0, 1001.0, 48000.0);
 *
 * Update the model every time a video frame is received:
 *   hires_av_rx(&havctx, HIRES_AV_STREAM_VIDEO, 1);
 *
 * Update the model every time a video frame is transmitted:
 *   hires_av_tx(&havctx, HIRES_AV_STREAM_VIDEO, 1);
 *
 * Update the model every time an audio frame of samples is received, with the sample count:
 *   hires_av_rx(&havctx, HIRES_AV_STREAM_VIDEO, 800);
 *
 * Update the model every time an audio frame of samples is mitted, with the sample count:
 *   hires_av_tx(&havctx, HIRES_AV_STREAM_VIDEO, 800);
 *
 * When the wrapping application requests, dump timing calculations to file descriptor (console):
 *   hires_av_summary(&havctx, 0);
 * Or, call this many times per second, and expect a console dump once per second:
 *   hires_av_summary_per_second(&havctx, 0);
 */

struct hires_av_stream_s
{
	/* Video I/O */
	double units_rx; /* Received from upstream */
	double units_tx; /* Schedules to h/w */
	struct timeval unit_rx_first;
	struct timeval unit_rx_current;
	double unit_rate_hz; /* 59.94 or 48000 */
	double unit_rate_us; /* unit_rate_hz * 1e6 */

	/* Computed on every update */
	double expected_units_rx; /* elapsed * unit_rate_us */

	/* Number of units WE SHOULD have received in the timeframe and
	 * any drifting slipping we're measuring.
	 */
	double expected_actual_deficit;
	double expected_actual_deficit_ms;

	struct timeval elapsed_time;
	double elapsed_time_us;
};

#define HIRES_AV_STREAM_VIDEO 0
#define HIRES_AV_STREAM_AP1   1
struct hires_av_ctx_s
{
	time_t lastSummary;
	struct hires_av_stream_s stream[2];
};

__inline__ int hires_av_timeval_subtract(struct timeval *result, struct timeval *x, struct timeval *y)
{
     /* Perform the carry for the later subtraction by updating y. */
     if (x->tv_usec < y->tv_usec)
     {
         int nsec = (y->tv_usec - x->tv_usec) / 1000000 + 1;
         y->tv_usec -= 1000000 * nsec;
         y->tv_sec += nsec;
     }
     if (x->tv_usec - y->tv_usec > 1000000)
     {
         int nsec = (x->tv_usec - y->tv_usec) / 1000000;
         y->tv_usec += 1000000 * nsec;
         y->tv_sec -= nsec;
     }

     /* Compute the time remaining to wait. tv_usec is certainly positive. */
     result->tv_sec = x->tv_sec - y->tv_sec;
     result->tv_usec = x->tv_usec - y->tv_usec;

     /* Return 1 if result is negative. */
     return x->tv_sec < y->tv_sec;
}

__inline__ void hires_av_init(struct hires_av_ctx_s *ctx, double num, double den, double audioSamplesPerHz)
{
	/* Video */
	ctx->stream[HIRES_AV_STREAM_VIDEO].units_rx = 0;
	ctx->stream[HIRES_AV_STREAM_VIDEO].units_tx = 0;
	ctx->stream[HIRES_AV_STREAM_VIDEO].unit_rx_first.tv_sec = 0;
	ctx->stream[HIRES_AV_STREAM_VIDEO].unit_rx_first.tv_usec = 0;
	ctx->stream[HIRES_AV_STREAM_VIDEO].unit_rx_current.tv_sec = 0;
	ctx->stream[HIRES_AV_STREAM_VIDEO].unit_rx_current.tv_usec = 0;

	/* 60000.0 / 1000.0;  59.9400599401 */
	ctx->stream[HIRES_AV_STREAM_VIDEO].unit_rate_hz = num / den;
	ctx->stream[HIRES_AV_STREAM_VIDEO].unit_rate_us = ctx->stream[HIRES_AV_STREAM_VIDEO].unit_rate_hz / 1e6;


	/* Audio */
	ctx->stream[HIRES_AV_STREAM_AP1].units_rx = 0;
	ctx->stream[HIRES_AV_STREAM_AP1].units_tx = 0;
	ctx->stream[HIRES_AV_STREAM_AP1].unit_rx_first.tv_sec = 0;
	ctx->stream[HIRES_AV_STREAM_AP1].unit_rx_first.tv_usec = 0;
	ctx->stream[HIRES_AV_STREAM_AP1].unit_rx_current.tv_sec = 0;
	ctx->stream[HIRES_AV_STREAM_AP1].unit_rx_current.tv_usec = 0;

	ctx->stream[HIRES_AV_STREAM_AP1].unit_rate_hz = audioSamplesPerHz;
	ctx->stream[HIRES_AV_STREAM_AP1].unit_rate_us = audioSamplesPerHz / 1e6;
}

__inline__ void hires_av_tx(struct hires_av_ctx_s *ctx, int nr, double adjust)
{
	ctx->stream[nr].units_tx += adjust;
}

__inline__ void hires_av_rx(struct hires_av_ctx_s *ctx, int nr, double adjust)
{
	gettimeofday(&ctx->stream[nr].unit_rx_current, NULL);

	if (ctx->stream[nr].units_rx == 0) {
		ctx->stream[nr].unit_rx_first = ctx->stream[nr].unit_rx_current;
	}

	ctx->stream[nr].units_rx += adjust;

	/* Calculate exact time (to us precision) between the first and current unit frames. */
	hires_av_timeval_subtract(&ctx->stream[nr].elapsed_time,
		&ctx->stream[nr].unit_rx_current, &ctx->stream[nr].unit_rx_first);

	/* Compute exactly how many units I should have transmitted, in a timebase of us. */
	/* elapsed time in us * unit_rate_us */
	ctx->stream[nr].elapsed_time_us  = ctx->stream[nr].elapsed_time.tv_sec * 1e6;
	ctx->stream[nr].elapsed_time_us += ctx->stream[nr].elapsed_time.tv_usec;

	ctx->stream[nr].expected_units_rx = ctx->stream[nr].elapsed_time_us * ctx->stream[nr].unit_rate_us;

	ctx->stream[nr].expected_actual_deficit = ctx->stream[nr].expected_units_rx - ctx->stream[nr].units_rx;
	ctx->stream[nr].expected_actual_deficit_ms = ctx->stream[nr].expected_actual_deficit / (ctx->stream[nr].unit_rate_us * 1000);
}

__inline__ void hires_av_summary_unit(struct hires_av_ctx_s *ctx, int fd, int nr)
{
	time_t now;
	time(&now);

	dprintf(fd, "strm#%d: @ %s", nr, ctime(&now));
	dprintf(fd, "  rx % 12f ", ctx->stream[nr].units_rx);
	dprintf(fd, "  tx % 12f\n", ctx->stream[nr].units_tx);
	dprintf(fd, "  first %ld.%.6ld",
		ctx->stream[nr].unit_rx_first);
	dprintf(fd, " current %d.%d",
		ctx->stream[nr].unit_rx_current.tv_sec,
		ctx->stream[nr].unit_rx_current.tv_usec);
	dprintf(fd, " elapsed %d.%d\n",
		ctx->stream[nr].elapsed_time.tv_sec,
		ctx->stream[nr].elapsed_time.tv_usec);
	dprintf(fd, "  elapsed_time_us % 12f\n", ctx->stream[nr].elapsed_time_us);
	dprintf(fd, "  unit_rate_hz %f\n", ctx->stream[nr].unit_rate_hz);
	dprintf(fd, "  unit_rate_us %f\n", ctx->stream[nr].unit_rate_us);
	dprintf(fd, "  expected_units_rx % 12f\n", ctx->stream[nr].expected_units_rx);
	dprintf(fd, "  expected minus rx actual % 12f (frames)\n", ctx->stream[nr].expected_actual_deficit);
	dprintf(fd, "  expected minus rx actual % 12.02f (ms)\n", ctx->stream[nr].expected_actual_deficit_ms);
}

__inline__ void hires_av_summary(struct hires_av_ctx_s *ctx, int fd)
{
	hires_av_summary_unit(ctx, fd, HIRES_AV_STREAM_VIDEO);
	//hires_av_summary_unit(ctx, fd, HIRES_AV_STREAM_AP1);
}

__inline__ void hires_av_summary_per_second(struct hires_av_ctx_s *ctx, int fd)
{
	time_t now;
	time(&now);

	if (ctx->lastSummary != now) {
		ctx->lastSummary = now;
		hires_av_summary(ctx, fd);
	}
}

#endif /* HIRES_AV_DEBUG_H */
