/* Copyright (c) 2014-2020 Kernel Labs Inc. All Rights Reserved. */

#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>
#include <assert.h>
#if HAVE_CURSES_H
#include <curses.h>
#endif
#include <libgen.h>
#include <signal.h>
#include <limits.h>
#include <libklvanc/vanc.h>
#include <libklvanc/smpte2038.h>
#include "smpte337_detector.h"
#include "frame-writer.h"
#include "rcwt.h"
#include "v210burn.h"

#include "hires-av-debug.h"
#include "kl-lineartrend.h"
#include "blackmagic-utils.h"
#include "bw-flash-av-offset.h"

#if HAVE_LIBKLMONITORING_KLMONITORING_H
#include <libklmonitoring/klmonitoring.h>
#endif

#if HAVE_IMONITORSDKPROCESSOR_H
#define ENABLE_NIELSEN 1
#endif

#if ENABLE_NIELSEN
#include "nielsen.h"
#endif

#include "hexdump.h"
#include "version.h"
#include "DeckLinkAPI.h"
#include "ts_packetizer.h"
#include "histogram.h"
#include "decklink_portability.h"

/* Forward declarations */
static void convert_colorspace_and_parse_vanc(unsigned char *buf, unsigned int uiWidth, unsigned int lineNr);

#define WIDE 80

class DeckLinkCaptureDelegate : public IDeckLinkInputCallback
{
public:
	DeckLinkCaptureDelegate();
	~DeckLinkCaptureDelegate();

	virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, LPVOID * ppv) { return E_NOINTERFACE; }
	virtual ULONG STDMETHODCALLTYPE AddRef(void);
	virtual ULONG STDMETHODCALLTYPE Release(void);
	virtual HRESULT STDMETHODCALLTYPE VideoInputFormatChanged(BMDVideoInputFormatChangedEvents, IDeckLinkDisplayMode *, BMDDetectedVideoInputFormatFlags);
	virtual HRESULT STDMETHODCALLTYPE VideoInputFrameArrived(IDeckLinkVideoInputFrame *, IDeckLinkAudioInputPacket *); 

private:
	ULONG m_refCount;
	pthread_mutex_t m_mutex;
};

#define RELEASE_IF_NOT_NULL(obj) \
        if (obj != NULL) { \
                obj->Release(); \
                obj = NULL; \
        }

/*
48693539 Mode:  9 HD 1080i 59.94    Mode:  6 HD 1080p 29.97  

Decklink Hardware supported modes:
[decklink @ 0x25da300] Mode:  0 NTSC                     6e747363 [ntsc]
[decklink @ 0x25da300] Mode:  1 NTSC 23.98               6e743233 [nt23]
[decklink @ 0x25da300] Mode:  2 PAL                      70616c20 [pal ]
[decklink @ 0x25da300] Mode:  3 HD 1080p 23.98           32337073 [23ps]
[decklink @ 0x25da300] Mode:  4 HD 1080p 24              32347073 [24ps]
[decklink @ 0x25da300] Mode:  5 HD 1080p 25              48703235 [Hp25]
[decklink @ 0x25da300] Mode:  6 HD 1080p 29.97           48703239 [Hp29]
[decklink @ 0x25da300] Mode:  7 HD 1080p 30              48703330 [Hp30]
[decklink @ 0x25da300] Mode:  8 HD 1080i 50              48693530 [Hi50]
[decklink @ 0x25da300] Mode:  9 HD 1080i 59.94           48693539 [Hi59]
[decklink @ 0x25da300] Mode: 10 HD 1080i 60              48693630 [Hi60]
[decklink @ 0x25da300] Mode: 11 HD 720p 50               68703530 [hp50]
[decklink @ 0x25da300] Mode: 12 HD 720p 59.94            68703539 [hp59]
[decklink @ 0x25da300] Mode: 13 HD 720p 60               68703630 [hp60]
*/

/* Signal Monitoring */
static int g_monitorSignalStability = 0;
static int g_hist_print_interval = 60;
static struct ltn_histogram_s *hist_arrival_interval = NULL;
static struct ltn_histogram_s *hist_arrival_interval_video = NULL;
static struct ltn_histogram_s *hist_arrival_interval_audio = NULL;
static struct ltn_histogram_s *hist_audio_sfc = NULL;
static struct ltn_histogram_s *hist_format_change = NULL;
/* End Signal Monitoring */

static BMDDisplayMode selectedDisplayMode = bmdModeNTSC;
static struct klvanc_context_s *vanchdl;
static pthread_mutex_t sleepMutex;
static pthread_cond_t sleepCond;
static int videoOutputFile = -1;
static int audioOutputFile = -1;

static int g_silencemax = -1;
static struct fwr_session_s *writeSession = NULL;

static int vancOutputFile = -1;
static int rcwtOutputFile = -1;
static int g_showStartupMemory = 0;
static int g_verbose = 0;
static unsigned int g_linenr = 0;
static uint64_t lastGoodKLFrameCounter = 0;
static uint64_t lastGoodKLOsdCounter = 0;

/* SMPTE 2038 */
static int g_packetizeSMPTE2038 = 0;
static int g_packetizePID = 0;
static struct klvanc_smpte2038_packetizer_s *smpte2038_ctx = 0;
static uint8_t g_cc = 0;
/* END:SMPTE 2038 */

static IDeckLink *deckLink;
static IDeckLinkInput *deckLinkInput;
static IDeckLinkDisplayModeIterator *displayModeIterator;

static BMDTimecodeFormat g_timecodeFormat = 0;
static uint32_t g_audioChannels = 16;
static uint32_t g_audioSampleDepth = 32;
static const char *g_videoOutputFilename = NULL;
static const char *g_audioOutputFilename = NULL;
static const char *g_audioInputFilename = NULL;
static const char *g_vancOutputFilename = NULL;
static const char *g_vancInputFilename = NULL;
static const char *g_vancOutputDir = NULL; /* Dir prefix to use, when saving VANC packets to disk. */
static const char *g_muxedOutputFilename = NULL;
static int g_muxedOutputExcludeVideo = 0;
static int g_muxedOutputExcludeAudio = 0;
static int g_muxedOutputExcludeData = 0;
static const char *g_muxedInputFilename = NULL;
static const char *g_rcwtOutputFilename = NULL;
static struct fwr_session_s *muxedSession = NULL;
static int g_maxFrames = -1;
static int g_shutdown = 0;
static int g_monitor_reset = 0;
static int g_monitor_mode = 0;
static int g_no_signal = 1;
static int g_kl_osd_vanc_compare = 0;
static BMDDisplayMode g_detected_mode_id = selectedDisplayMode;
static BMDDisplayMode g_requested_mode_id = 0;
static BMDVideoInputFlags g_inputFlags = bmdVideoInputEnableFormatDetection;
static BMDPixelFormat g_pixelFormat = bmdFormat10BitYUV;
struct fwr_header_timing_s ftfirst, ftlast;

/* 1602 + 1601 + 1602 + 1601 + 1602 = 8008
 * 48000 / 8008 = 5.994
 */
static int g_1080i2997_cadence_check = 0;
static int g_1080i2997_cadence_set[5]  = { 1602, 1601, 1602, 1601, 1602 }; /* 48KHz 2997 */
static int g_1080i2997_cadence_match[5]= {    0,    0,    0,    0,    0 };
static int g_1080i2997_cadence_hist[5] = {    0,    0,    0,    0,    0 };

static int g_hires_av_debug = 0;
static struct hires_av_ctx_s g_havctx;
static struct kllineartrend_context_s *g_trendctx;

static unsigned int g_analyzeBitmask = 0;

static int g_enable_smpte337_detector = 0;

static struct bw_flash_avoffset_ctx_s g_bw_flash_ctx = { 0 };
static int g_bw_flash_measurements = 0;
static int g_bw_flash_initialized = 0;

#if HAVE_LIBKLMONITORING_KLMONITORING_H
static int g_monitor_prbs_audio_mode = 0;
static struct prbs_context_s g_prbs;
static int g_prbs_initialized = 0;
#endif

#if ENABLE_NIELSEN
static int g_enable_nielsen = 0;
/* We're assuming a max of 8 pairs */
CMonitorApi *pNielsenAPI[16] = { 0 };
CMonitorSdkParameters *pNielsenParams[16] = { 0 };
CMonitorSdkCallback *pNielsenCallback[16] = { 0 };
#endif

static unsigned long audioFrameCount = 0;
static struct frameTime_s {
	unsigned long long lastTime;
	unsigned long long frameCount;
	unsigned long long remoteFrameCount;
} frameTimes[2];

#if HAVE_LIBKLMONITORING_KLMONITORING_H
static void dumpAudio(uint16_t *ptr, int fc, int num_channels)
{
	fc = 4;
	uint32_t *p = (uint32_t *)ptr;
	for (int i = 0; i < fc; i++) {
		printf("%d.", i);
		for (int j = 0; j < num_channels; j++)
			printf("%08x ", *p++);
		printf("\n");
	}
}
#endif

void genericDumpAudioPayload(IDeckLinkAudioInputPacket* audioFrame, int audioChannelCount, int audioSampleDepth)
{
	assert(audioChannelCount == 16);
	assert(audioSampleDepth == 32);

	if (!audioFrame)
		return;

	uint8_t *data = NULL;
	audioFrame->GetBytes((void **)&data);

	uint32_t *p = (uint32_t *)data;

	for (int s = 0; s < audioFrame->GetSampleFrameCount(); s++) {
		printf("%06d : ", s);
		for (int i = 0; i < audioChannelCount; i++) {
			printf("%08x ", *p);
			p++;
		}
		printf("\n");
	}
	printf("\n\n");

}

struct audioSilenceContext_s
{
	time_t lastReport;
	double sequentialAudioSilenceMs;

	int sequentialAudioSilence;
	int sequentialAudioSilenceLast;

} g_asctx[16];

void checkForSilence(IDeckLinkAudioInputPacket* audioFrame, int channelNr, int audioChannelCount, int audioSampleDepth)
{
	assert(audioChannelCount == 16);
	assert(audioSampleDepth == 32);

	if (!audioFrame)
		return;

	struct audioSilenceContext_s *asctx = &g_asctx[channelNr];

	time_t now;
	time(&now);
	if (now != asctx->lastReport && asctx->sequentialAudioSilenceMs > 0) {
		printf("channel %d: %7.2fms of silent audio @ %s",
			channelNr,
			asctx->sequentialAudioSilenceMs,
			ctime(&asctx->lastReport));
		asctx->lastReport = now;
		asctx->sequentialAudioSilenceMs = 0;
	}

	uint8_t *data = NULL;
	audioFrame->GetBytes((void **)&data);

	uint32_t *p = (uint32_t *)data;
	p += channelNr; /* Adjust the offset to the start of the channel we are inspecting */
	int silence = 0;
	// int lastSilenceIdx = -1;

	/* 720p59.94 default to 24, 1080i default to 48 */
	int limit = 24;
	if (audioFrame->GetSampleFrameCount() > 800)
		limit = 48;

	/* Operator can override the upper limit */
	if (g_silencemax != -1)
		limit = g_silencemax;

	for (int s = 0; s < audioFrame->GetSampleFrameCount(); s++) {
		uint32_t dw = *p;
		if (dw == 0) {
			//printf("silence at %d last %d\n", s, lastSilenceIdx);
			silence++;
			asctx->sequentialAudioSilence++;
		} else {
			asctx->sequentialAudioSilence = 0;
		}
		//printf("%08x\n", dw);

		p += audioChannelCount;
	}

	if (silence >= limit) {
		time_t now;
		time(&now);
		double lostMS = (double)silence / 48.0;
		asctx->sequentialAudioSilenceMs += lostMS;
		printf("\tSilence detected on channel %d, lost %5.02fms (or #%5d samples) @ %s",
			channelNr,
			lostMS,
			silence, ctime(&now));
		fflush(stdout); /* When console is redirected to logs, we want output in logs immediately. */
		if (channelNr == 0) {
			//genericDumpAudioPayload(audioFrame, audioChannelCount, audioSampleDepth);
		}
	}
}

#if HAVE_CURSES_H
static pthread_t g_monitor_draw_threadId;
static pthread_t g_monitor_input_threadId;

static void cursor_save_all()
{
	for (int d = 0; d <= 0xff; d++) {
		for (int s = 0; s <= 0xff; s++) {
			struct klvanc_cache_s *e = klvanc_cache_lookup(vanchdl, d, s);
			if (!e->activeCount)
				continue;
			e->save = 1;
		}
	}
}

static void cursor_expand_all()
{
	for (int d = 0; d <= 0xff; d++) {
		for (int s = 0; s <= 0xff; s++) {
			struct klvanc_cache_s *e = klvanc_cache_lookup(vanchdl, d, s);
			if (!e->activeCount)
				continue;
			e->expandUI = 1;
		}
	}
}

static void cursor_expand()
{
	for (int d = 0; d <= 0xff; d++) {
		for (int s = 0; s <= 0xff; s++) {
			struct klvanc_cache_s *e = klvanc_cache_lookup(vanchdl, d, s);
			if (!e->activeCount)
				continue;

			if (e->hasCursor == 1) {
				if (e->expandUI)
					e->expandUI = 0;
				else
					e->expandUI = 1;
				return;
			}
		}
	}
}

static void cursor_down()
{
	struct klvanc_cache_s *def = 0;
	struct klvanc_cache_s *prev = 0;

	for (int d = 0; d <= 0xff; d++) {
		for (int s = 0; s <= 0xff; s++) {
			struct klvanc_cache_s *e = klvanc_cache_lookup(vanchdl, d, s);
			if (!e->activeCount)
				continue;

			def = e;
			if (e->hasCursor == 1 && !prev) {
				prev = e;
			} else
			if (!e->hasCursor && prev) {
				prev->hasCursor = 0;
				e->hasCursor = 1;
				return;
			}
		}
	}

	if (def)
		def->hasCursor = 1;
}

static void cursor_up()
{
	struct klvanc_cache_s *def = 0;
	struct klvanc_cache_s *prev = 0;

	for (int d = 0; d <= 0xff; d++) {
		for (int s = 0; s <= 0xff; s++) {
			struct klvanc_cache_s *e = klvanc_cache_lookup(vanchdl, d, s);
			if (!e->activeCount)
				continue;

			def = e;
			if (e->hasCursor == 0) {
				prev = e;
			} else
			if (e->hasCursor && prev) {
				prev->hasCursor = 1;
				e->hasCursor = 0;
				return;
			}
		}
	}

	if (def)
		def->hasCursor = 0;
}

static void vanc_monitor_stats_dump_curses()
{
	int linecount = 0;
	int headLineColor = 1;
	int cursorColor = 5;

	char head_c[160];
	if (g_no_signal)
		sprintf(head_c, "NO SIGNAL");
	else if (g_requested_mode_id != 0 && g_requested_mode_id != g_detected_mode_id) {
		sprintf(head_c, "CHECK SIGNAL SETTINGS %c%c%c%c", g_detected_mode_id >> 24,
			g_detected_mode_id >> 16, g_detected_mode_id >> 8, g_detected_mode_id);
		headLineColor = 4;
	} else {
		sprintf(head_c, "SIGNAL LOCKED %c%c%c%c", g_detected_mode_id >> 24,
			g_detected_mode_id >> 16, g_detected_mode_id >> 8, g_detected_mode_id);
	}

	char head_a[160];
	sprintf(head_a, " DID / SDID  DESCRIPTION");

	char head_b[160];
	int blen = (WIDE - 5) - (strlen(head_a) + strlen(head_c));
	memset(head_b, 0x20, sizeof(head_b));
	head_b[blen] = 0;

	attron(COLOR_PAIR(headLineColor));
	mvprintw(linecount++, 0, "%s%s%s", head_a, head_b, head_c);
        attroff(COLOR_PAIR(headLineColor));

	for (int d = 0; d <= 0xff; d++) {
		for (int s = 0; s <= 0xff; s++) {
			struct klvanc_cache_s *e = klvanc_cache_lookup(vanchdl, d, s);
			if (!e)
				continue;

			if (e->activeCount == 0)
				continue;

			{
				if (e->hasCursor)
					attron(COLOR_PAIR(cursorColor));
				char t[80];
				sprintf(t, "  %02x / %02x    %s [%s] ", e->did, e->sdid, e->desc, e->spec);
				mvprintw(linecount++, 0, "%-75s", t);
				if (e->hasCursor)
					attroff(COLOR_PAIR(cursorColor));
			}

			for (int l = 0; l < 2048; l++) {
				struct klvanc_cache_line_s *line = &e->lines[ l ];
				if (!line->active)
					continue;

				pthread_mutex_lock(&line->mutex);
				struct klvanc_packet_header_s *pkt = line->pkt;

				mvprintw(linecount++, 13, "line #%d count #%lu horizontal offset word #%d", l, line->count,
					pkt->horizontalOffset);

				if (e->save)
				{
					klvanc_packet_save("/tmp", pkt, -1, -1);
					e->save = 0;
				}

				if (e->expandUI)
				{
					mvprintw(linecount++, 13, "data length: 0x%x (%d)",
						pkt->payloadLengthWords,
						pkt->payloadLengthWords);

					char p[256] = { 0 };
					int cnt = 0;
					for (int w = 0; w < pkt->payloadLengthWords; w++) {
						sprintf(p + strlen(p), "%02x ", (pkt->payload[w]) & 0xff);
						if (++cnt == 16 || (w + 1) == pkt->payloadLengthWords) {
							cnt = 0;
							if (w == 15 || (pkt->payloadLengthWords < 15))
								mvprintw(linecount++, 13, "  -> %s", p);
							else
								mvprintw(linecount++, 13, "     %s", p);
							p[0] = 0;
						}
					}
					mvprintw(linecount++, 13, "checksum %03x (%s)",
						pkt->checksum,
						pkt->checksumValid ? "VALID" : "INVALID");
				}
				pthread_mutex_unlock(&line->mutex);
			}

			linecount++;
		}
	}

	if (g_kl_osd_vanc_compare) {
		mvprintw(linecount++, 2, "KL VANC/OSD Frame Counter synchronization");

		mvprintw(linecount++, 2, "video=%d vanc=%d delta=%d\n", lastGoodKLOsdCounter, lastGoodKLFrameCounter,
			 lastGoodKLOsdCounter - lastGoodKLFrameCounter);
		linecount++;
	}

	attron(COLOR_PAIR(2));
        mvprintw(linecount++, 0, "q)uit r)eset e)xpand S)ave all E)xpand all");
	attroff(COLOR_PAIR(2));

	char tail_c[160];
	time_t now = time(0);
	sprintf(tail_c, "%s", ctime(&now));

	char tail_a[160];
	sprintf(tail_a, "KLVANC_CAPTURE");

	char tail_b[160];
	blen = (WIDE - 4) - (strlen(tail_a) + strlen(tail_c));
	memset(tail_b, 0x20, sizeof(tail_b));
	tail_b[blen] = 0;

	attron(COLOR_PAIR(1));
	mvprintw(linecount++, 0, "%s%s%s", tail_a, tail_b, tail_c);
        attroff(COLOR_PAIR(1));
}

static void vanc_monitor_stats_dump()
{
	for (int d = 0; d <= 0xff; d++) {
		for (int s = 0; s <= 0xff; s++) {
			struct klvanc_cache_s *e = klvanc_cache_lookup(vanchdl, d, s);
			if (!e)
				continue;

			if (e->activeCount == 0)
				continue;

			printf("->did/sdid = %02x / %02x: %s [%s] ", e->did, e->sdid, e->desc, e->spec);
			for (int l = 0; l < 2048; l++) {
				if (e->lines[l].active)
					printf("via SDI line %d (%" PRIu64 " packets) ", l, e->lines[l].count);
			}
			printf("\n");
		}
	}
}

static void signal_handler(int signum);
static void *thread_func_input(void *p)
{
	while (!g_shutdown) {
		int ch = getch();
		if (ch == 'q') {
			signal_handler(1);
			break;
		}
		if (ch == 'r')
			g_monitor_reset = 1;
		if (ch == 'e')
			cursor_expand();
		if (ch == 'S')
			cursor_save_all();
		if (ch == 'E')
			cursor_expand_all();
		if (ch == 0x1b) {
			ch = getch();

			/* Cursor keys */
			if (ch == 0x5b) {
				ch = getch();
				if (ch == 0x41) {
					/* Up arrow */
					cursor_up();
				} else
				if (ch == 0x42) {
					/* Down arrow */
					cursor_down();
				} else
				if (ch == 0x43) {
					/* Right arrow */
				} else
				if (ch == 0x44) {
					/* Left arrow */
				}
			}
		}
	}
	return 0;
}

static void *thread_func_draw(void *p)
{
	noecho();
	curs_set(0);
	start_color();
	init_pair(1, COLOR_WHITE, COLOR_BLUE);
	init_pair(2, COLOR_CYAN, COLOR_BLACK);
	init_pair(3, COLOR_RED, COLOR_BLACK);
	init_pair(4, COLOR_RED, COLOR_BLUE);
	init_pair(5, COLOR_BLACK, COLOR_WHITE);

	while (!g_shutdown) {
		if (g_monitor_reset) {
			g_monitor_reset = 0;
			klvanc_cache_reset(vanchdl);
		}

		clear();
		vanc_monitor_stats_dump_curses();

		refresh();
		usleep(100 * 1000);
	}

	return 0;
}
#endif /* HAVE_CURSES_H */

static void signal_handler(int signum)
{
	if (signum == SIGUSR1) {
		ltn_histogram_interval_print(STDOUT_FILENO, hist_arrival_interval, 0);
		ltn_histogram_interval_print(STDOUT_FILENO, hist_arrival_interval_video, 0);
		ltn_histogram_interval_print(STDOUT_FILENO, hist_arrival_interval_audio, 0);
		ltn_histogram_interval_print(STDOUT_FILENO, hist_audio_sfc, 0);
		ltn_histogram_interval_print(STDOUT_FILENO, hist_format_change, 0);

		hires_av_summary(&g_havctx, 0); /* Write stats to console */

	} else
	if (signum == SIGUSR2) {
		printf("Stats manually reset via SIGUSR2\n");
		ltn_histogram_reset(hist_arrival_interval);
		ltn_histogram_reset(hist_arrival_interval_video);
		ltn_histogram_reset(hist_arrival_interval_audio);
		ltn_histogram_reset(hist_audio_sfc);
		ltn_histogram_reset(hist_format_change);
	} else {
		g_shutdown = 1;
		pthread_cond_signal(&sleepCond);
	}
}

static void showMemory(FILE * fd)
{
	char fn[64];
	char s[80];
	sprintf(fn, "/proc/%d/statm", getpid());

	FILE *fh = fopen(fn, "rb");
	if (!fh)
		return;

	memset(s, 0, sizeof(s));
	size_t wlen = fread(s, 1, sizeof(s) - 1, fh);
	fclose(fh);

	if (wlen > 0) {
		fprintf(fd, "%s: %s", fn, s);
	}
}

static unsigned long long msecsX10()
{
	unsigned long long elapsedMs;

	struct timeval now;
	gettimeofday(&now, 0);

	elapsedMs = (now.tv_sec * 10000.0);	/* sec to ms */
	elapsedMs += (now.tv_usec / 100.0);	/* us to ms */

	return elapsedMs;
}

static char g_mode[5];		/* Racey */
static const char *display_mode_to_string(BMDDisplayMode m)
{
	g_mode[4] = 0;
	g_mode[3] = m;
	g_mode[2] = m >> 8;
	g_mode[1] = m >> 16;
	g_mode[0] = m >> 24;

	return &g_mode[0];
}

static void *smpte337_callback(void *user_context, struct smpte337_detector_s *ctx, uint8_t datamode, uint8_t datatype, uint32_t payload_bitCount, uint8_t *payload)
{
	printf("%s() mode: %d  type: %d\n", __func__, datamode, datatype);
	return 0;
}

static int AnalyzeMuxed(const char *fn)
{
	struct fwr_session_s *session;
	if (fwr_session_file_open(fn, 0, &session) < 0) {
		fprintf(stderr, "Error opening %s\n", fn);
		return -1;
	}

	struct fwr_header_audio_s *fa;
	struct fwr_header_video_s *fv;
	struct fwr_header_timing_s ft;
	struct fwr_header_vanc_s *fd;
	uint32_t header;

	while (1) {
		fa = 0, fv = 0;

		if (fwr_session_frame_gettype(session, &header) < 0) {
			break;
		}

		if (header == timing_v1_header) {
			ftlast = ft;
			if (fwr_timing_frame_read(session, &ft) < 0) {
				break;
			}
			struct timeval diff;
			fwr_timeval_subtract(&diff, &ft.ts1, &ftlast.ts1);

			printf("timing: counter %" PRIu64 "  mode:%s  ts:%ld.%06ld  timestamp_interval:%ld.%06ld\n",
				ft.counter,
				display_mode_to_string(ft.decklinkCaptureMode),
				ft.ts1.tv_sec,
				ft.ts1.tv_usec,
				diff.tv_sec,
				diff.tv_usec);
		} else
		if (header == video_v1_header) {
			if (fwr_video_frame_read(session, &fv) < 0) {
				fprintf(stderr, "No more video?\n");
				break;
			}
			printf("\tvideo: %d x %d  strideBytes: %d  bufferLengthBytes: %d\n",
				fv->width, fv->height, fv->strideBytes, fv->bufferLengthBytes);
		} else
		if (header == VANC_SOL_INDICATOR) {
			if (fwr_vanc_frame_read(session, &fd) < 0) {
				fprintf(stderr, "No more vanc?\n");
				break;
			}
			printf("\t\tvanc: line: %4d -- ", fd->line);
			for (int i = 0; i < 32; i++)
				printf("%02x ", *(fd->ptr + i));
			printf("\n");
			/* Process the line colorspace, hand-off to the vanc library for parsing
			 * and prepare to receive callbacks.
			 */
			convert_colorspace_and_parse_vanc(fd->ptr, fd->width, fd->line);

		} else
		if (header == audio_v1_header) {
			if (fwr_pcm_frame_read(session, &fa) < 0) {
				break;
			}
			printf("\taudio: channels: %d  depth: %d  frameCount: %d  bufferLengthBytes: %d\n",
				fa->channelCount,
				fa->sampleDepth,
				fa->frameCount,
				fa->bufferLengthBytes);
		}

		if (fa) {
			fwr_pcm_frame_free(session, fa);
			fa = 0;
		}
		if (fv) {
			fwr_video_frame_free(session, fv);
			fv = 0;
		}
	}

	fwr_session_file_close(session);
	return 0;
}

static int AnalyzeAudio(const char *fn)
{
	struct fwr_session_s *session;
	if (fwr_session_file_open(fn, 0, &session) < 0) {
		fprintf(stderr, "Error opening %s\n", fn);
		return -1;
	}

	uint32_t frame = 0;

	struct smpte337_detector_s *det[8] = { 0 };
	FILE *ofh[8] = { 0 };

	for (int i = 0; i < 8; i++) {
		if (g_enable_smpte337_detector) {
			det[i] = smpte337_detector_alloc((smpte337_detector_callback)smpte337_callback, (void *)det[i]);
		}

		/* Friendly note:
		 * Convert the PCM into wav using: avconv -y -f s32le -ar 48k -ac 2 -i pairX.bin fileX.wav.
		 */
		char name[PATH_MAX];
		snprintf(name, sizeof(name), "%s-pair%d.raw", g_audioInputFilename, i);
		ofh[i] = fopen(name, "wb");
	}

	struct fwr_header_audio_s *f;

	while (1) {
		uint32_t header;
		if (fwr_session_frame_gettype(session, &header) < 0) {
			break;
		}
		if (header != audio_v1_header || fwr_pcm_frame_read(session, &f) < 0) {
			continue;
		}

		frame++;

		uint32_t stride = f->channelCount * (f->sampleDepth / 8);

		printf("id: %8d ch: %d  sfc: %d  depth: %d  stride: %d  bytes: %d\n",
			frame - 1, f->channelCount, f->frameCount, f->sampleDepth, stride, f->bufferLengthBytes);
		if (g_verbose) {
			for (unsigned int i = 0; i < f->frameCount; i++) {
				printf("   frame: %8d  ", i);
				for (unsigned int j = 0; j < stride; j++) {
					if (j && (f->sampleDepth == 32) && ((j % 8) == 0))
						printf(": ");
					printf("%02x ", *(f->ptr + (i * stride) + j));
				}
				printf("\n");
			}
		}

		if (g_enable_smpte337_detector) {
			for (int i = 0; i < 8; i++) {
				int offset = (i * 2) * (f->sampleDepth / 8);
				smpte337_detector_write(det[i], f->ptr + offset,
							f->frameCount, f->sampleDepth, f->channelCount, stride, 1);
			}
		}

		/* Dump each L/R pair to a seperate file. */
		unsigned char *p = f->ptr;
		for (unsigned int i = 0; i < f->frameCount; i++) {
			for (unsigned int j = 0; j < (f->channelCount / 2); j++) {
				fwrite(p, 2 * (f->sampleDepth / 8), 1, ofh[j]);
				p += 2 * (f->sampleDepth / 8);
			}
		}

		fwr_pcm_frame_free(session, f);
	}
	for (int i = 0; i < 8; i++) {
		if (g_enable_smpte337_detector)
			smpte337_detector_free(det[i]);
		fclose(ofh[i]);
	}

	fwr_session_file_close(session);

	return 0;
}

static void convert_colorspace_and_parse_vanc(unsigned char *buf, unsigned int uiWidth, unsigned int lineNr)
{
	/* Convert the vanc line from V210 to CrCB422, then vanc parse it */

	/* We need two kinds of type pointers into the source vbi buffer */
	/* TODO: What the hell is this, two ptrs? */
	const uint32_t *src = (const uint32_t *)buf;

	/* Convert Blackmagic pixel format to nv20.
	 * src pointer gets mangled during conversion, hence we need its own
	 * ptr instead of passing vbiBufferPtr */
	uint16_t decoded_words[16384];
	memset(&decoded_words[0], 0, sizeof(decoded_words));
	uint16_t *p_anc = decoded_words;

	if (uiWidth == 720) {
		/* Standard definition video will have VANC spanning both
		   Luma and Chroma channels */
		klvanc_v210_line_to_uyvy_c(src, p_anc, uiWidth);
	} else {
		if (klvanc_v210_line_to_nv20_c(src, p_anc,
					       sizeof(decoded_words),
					       (uiWidth / 6) * 6) < 0)
			return;
	}

	/* Don't attempt to parse vanc if we're capturing it and the monitor isn't running. */
	if (!g_monitor_mode && vancOutputFile >= 0)
		return;

	int ret = klvanc_packet_parse(vanchdl, lineNr, decoded_words, sizeof(decoded_words) / (sizeof(unsigned short)));
	if (ret < 0) {
		/* No VANC on this line */
	}
}

#define TS_OUTPUT_NAME "/tmp/smpte2038-sample.ts"
static int AnalyzeVANC(const char *fn)
{
	FILE *fh = fopen(fn, "rb");
	if (!fh) {
		fprintf(stderr, "Unable to open [%s]\n", fn);
		return -1;
	}

	fseek(fh, 0, SEEK_END);
	fprintf(stdout, "Analyzing VANC file [%s] length %lu bytes\n", fn, ftell(fh));
	fseek(fh, 0, SEEK_SET);

	unsigned int uiSOL;
	unsigned int uiLine;
	unsigned int uiWidth;
	unsigned int uiHeight;
	unsigned int uiStride;
	unsigned int uiEOL;
	unsigned int maxbuflen = 16384;
	unsigned char *buf = (unsigned char *)malloc(maxbuflen);

	while (!feof(fh)) {

		/* Warning: Balance these reads with the file writes in processVANC */
		fread(&uiSOL, sizeof(unsigned int), 1, fh);
		fread(&uiLine, sizeof(unsigned int), 1, fh);
		fread(&uiWidth, sizeof(unsigned int), 1, fh);
		fread(&uiHeight, sizeof(unsigned int), 1, fh);
		fread(&uiStride, sizeof(unsigned int), 1, fh);
		memset(buf, 0, maxbuflen);
		fread(buf, uiStride, 1, fh);
		assert(uiStride < maxbuflen);
		fread(&uiEOL, sizeof(unsigned int), 1, fh);

		if (g_linenr && g_linenr != uiLine)
			continue;

		fprintf(stdout, "Line: %04d SOL: %x EOL: %x ", uiLine, uiSOL, uiEOL);
		fprintf(stdout, "Width: %d Height: %d Stride: %d ", uiWidth, uiHeight, uiStride);
		if (uiSOL != VANC_SOL_INDICATOR)
			fprintf(stdout, " SOL corrupt ");
		if (uiEOL != VANC_EOL_INDICATOR)
			fprintf(stdout, " EOL corrupt ");

		fprintf(stdout, "\n");

		if (g_verbose > 1)
			hexdump(buf, uiStride, 64);

		if (uiLine == 1 && g_packetizeSMPTE2038) {
			if (klvanc_smpte2038_packetizer_end(smpte2038_ctx, 0) == 0) {
				printf("%s() PES buffer is complete\n", __func__);

				uint8_t *pkts = 0;
				uint32_t packetCount = 0;
				if (ts_packetizer(smpte2038_ctx->buf, smpte2038_ctx->bufused, &pkts,
					&packetCount, 188, &g_cc, g_packetizePID) == 0) {
					FILE *fh = fopen(TS_OUTPUT_NAME, "a+");
					if (fh) {
						if (g_verbose) {
							printf("Writing %d SMPTE2038 TS packet(s) to %s\n",
								packetCount, TS_OUTPUT_NAME);
						}
						fwrite(pkts, packetCount, 188, fh);
						fclose(fh);
					}
					free(pkts);
				}
			}
			klvanc_smpte2038_packetizer_begin(smpte2038_ctx);
		}
		convert_colorspace_and_parse_vanc(buf, uiWidth, uiLine);
	}

	free(buf);
	fclose(fh);

	return 0;
}

#define COMPRESS 0
#if COMPRESS
static int cdstlen = 16384;
static uint8_t *cdstbuf = 0;
#endif
#define DECOMPRESS 0
#if DECOMPRESS
static int ddstlen = 16384;
static uint8_t *ddstbuf = 0;
#endif
static void ProcessVANC(IDeckLinkVideoInputFrame * frame)
{
	IDeckLinkVideoFrameAncillary *vanc;
	if (frame->GetAncillaryData(&vanc) != S_OK)
		return;

	if (g_packetizeSMPTE2038)
		klvanc_smpte2038_packetizer_begin(smpte2038_ctx);

	BMDDisplayMode dm = vanc->GetDisplayMode();
	BMDPixelFormat pf = vanc->GetPixelFormat();

	unsigned int uiStride = frame->GetRowBytes();
	unsigned int uiWidth = frame->GetWidth();
	unsigned int uiHeight = frame->GetHeight();
	unsigned int uiLine;
	unsigned int uiSOL = VANC_SOL_INDICATOR;
	unsigned int uiEOL = VANC_EOL_INDICATOR;
	int written = 0;
	for (unsigned int i = 0; i < uiHeight; i++) {
		uint8_t *buf;
		int ret = vanc->GetBufferForVerticalBlankingLine(i, (void **)&buf);
		if (ret != S_OK)
			continue;

		uiLine = i;

		/* Process the line colorspace, hand-off to the vanc library for parsing
		 * and prepare to receive callbacks.
		 */
		convert_colorspace_and_parse_vanc(buf, uiWidth, uiLine);

		if (muxedSession && g_muxedOutputExcludeData == 0 && buf) {
			struct fwr_header_vanc_s *frame = 0;
			if (fwr_vanc_frame_create(muxedSession, uiLine, uiWidth, uiHeight, uiStride, (uint8_t *)buf, &frame) == 0) {
				fwr_writer_enqueue(muxedSession, frame, FWR_FRAME_VANC);
			}
		}

		if (vancOutputFile >= 0) {
			/* Warning: Balance these writes with the file reads in AnalyzeVANC */
			write(vancOutputFile, &uiSOL, sizeof(unsigned int));
			write(vancOutputFile, &uiLine, sizeof(unsigned int));
			write(vancOutputFile, &uiWidth, sizeof(unsigned int));
			write(vancOutputFile, &uiHeight, sizeof(unsigned int));
			write(vancOutputFile, &uiStride, sizeof(unsigned int));
			write(vancOutputFile, buf, uiStride);
#if COMPRESS
			if (cdstbuf == 0)
				cdstbuf = (uint8_t *)malloc(cdstlen);

			/* Pack metadata into the pre-compress buffer */
			int z = 0;
			*(buf + z++) = uiLine >> 8;
			*(buf + z++) = uiLine;
			*(buf + z++) = uiWidth >> 8;
			*(buf + z++) = uiWidth;
			*(buf + z++) = uiHeight >> 8;
			*(buf + z++) = uiHeight;
			*(buf + z++) = uiStride >> 8;
			*(buf + z++) = uiStride;

			z_stream zInfo = { 0 };
			zInfo.total_out = zInfo.avail_out = cdstlen;
			zInfo.next_in = (uint8_t *)buf + z;
			zInfo.total_in = zInfo.avail_in = z + uiStride;
			zInfo.next_out = cdstbuf;
			memcpy(buf + z, buf, uiStride);

			int nErr = deflateInit(&zInfo, Z_DEFAULT_COMPRESSION);
			unsigned int compressLength = 0;
			if (nErr == Z_OK ) {
				nErr = deflate(&zInfo, Z_FINISH);
				if (nErr == Z_STREAM_END) {
					compressLength = zInfo.total_out;
					write(vancOutputFile, &compressLength, sizeof(unsigned int));
					write(vancOutputFile, cdstbuf, compressLength);
					if (g_verbose > 1)
						printf("Compressed %d bytes\n", compressLength);
				} else {
					fprintf(stderr, "Failed to compress payload\n");
				}
			}
			deflateEnd(&zInfo);
#endif
#if DECOMPRESS
			/* Decompress and verify */
			if (ddstbuf == 0)
				ddstbuf = (uint8_t *)malloc(ddstlen);

			z_stream dzInfo = { 0 };
			dzInfo.total_in = dzInfo.avail_in = compressLength;
			dzInfo.total_out = dzInfo.avail_out = ddstlen;
			dzInfo.next_in = (uint8_t *)cdstbuf;
			dzInfo.next_out = ddstbuf;

			nErr = inflateInit(&dzInfo);
			if (nErr == Z_OK) {
				nErr = inflate(&dzInfo, Z_FINISH);
				if (nErr == Z_STREAM_END) {
					if (memcmp(buf, ddstbuf, dzInfo.total_out) == 0) {
						/* Success */
					} else
						fprintf(stderr, "Decompress validation failed\n");
				} else
					fprintf(stderr, "Inflate error, %d\n", nErr);
			} else
				fprintf(stderr, "Decompress error, %d\n", nErr);
			inflateEnd(&dzInfo);
#endif
			write(vancOutputFile, &uiEOL, sizeof(unsigned int));

			written++;
		}

	}

	if (g_packetizeSMPTE2038) {
		BMDTimeValue stream_time;
		BMDTimeValue frame_duration;
		frame->GetStreamTime(&stream_time, &frame_duration, 90000);
		if (klvanc_smpte2038_packetizer_end(smpte2038_ctx, stream_time) == 0) {
			printf("%s() PES buffer is complete\n", __func__);
		}
	}

	if (g_verbose > 1) {
		fprintf(stdout, "PixelFormat %x [%s] DisplayMode [%s] Wrote %d [potential] VANC lines\n",
			pf,
			pf == bmdFormat8BitYUV ? "bmdFormat8BitYUV" :
			pf == bmdFormat10BitYUV ? "bmdFormat10BitYUV" :
			pf == bmdFormat8BitARGB ? "bmdFormat8BitARGB" :
			pf == bmdFormat8BitBGRA ? "bmdFormat8BitBGRA" :
			pf == bmdFormat10BitRGB ? "bmdFormat10BitRGB" : "undefined",
			display_mode_to_string(dm), written);
	}

	vanc->Release();

#if COMPRESS
	if (cdstbuf) {
		free(cdstbuf);
		cdstbuf = 0;
	}
#endif
#if DECOMPRESS
	if (ddstbuf) {
		free(ddstbuf);
		ddstbuf = 0;
	}
#endif
	return;
}

DeckLinkCaptureDelegate::DeckLinkCaptureDelegate()
: m_refCount(0)
{
	pthread_mutex_init(&m_mutex, NULL);
}

DeckLinkCaptureDelegate::~DeckLinkCaptureDelegate()
{
	pthread_mutex_destroy(&m_mutex);
}

ULONG DeckLinkCaptureDelegate::AddRef(void)
{
	pthread_mutex_lock(&m_mutex);
	m_refCount++;
	pthread_mutex_unlock(&m_mutex);

	return (ULONG) m_refCount;
}

ULONG DeckLinkCaptureDelegate::Release(void)
{
	pthread_mutex_lock(&m_mutex);
	m_refCount--;
	pthread_mutex_unlock(&m_mutex);

	if (m_refCount == 0) {
		delete this;
		return 0;
	}

	return (ULONG) m_refCount;
}

static void monitorSignal(IDeckLinkVideoInputFrame *videoFrame, IDeckLinkAudioInputPacket *audioFrame)
{
	ltn_histogram_interval_update(hist_arrival_interval);

	if (videoFrame)
		ltn_histogram_interval_update(hist_arrival_interval_video);

	if (audioFrame) {
		ltn_histogram_interval_update(hist_arrival_interval_audio);

		uint32_t sfc = audioFrame->GetSampleFrameCount();
		ltn_histogram_update_with_timevalue(hist_audio_sfc, sfc);

	}

	ltn_histogram_interval_print(STDOUT_FILENO, hist_arrival_interval, g_hist_print_interval);
	ltn_histogram_interval_print(STDOUT_FILENO, hist_arrival_interval_video, g_hist_print_interval);
	ltn_histogram_interval_print(STDOUT_FILENO, hist_arrival_interval_audio, g_hist_print_interval);
	ltn_histogram_interval_print(STDOUT_FILENO, hist_audio_sfc, g_hist_print_interval);
	ltn_histogram_interval_print(STDOUT_FILENO, hist_format_change, g_hist_print_interval);
}

HRESULT DeckLinkCaptureDelegate::VideoInputFrameArrived(IDeckLinkVideoInputFrame *videoFrame, IDeckLinkAudioInputPacket *audioFrame)
{
	if (g_shutdown == 1) {
		g_shutdown = 2;
		return S_OK;
	}
	if (g_shutdown == 2)
		return S_OK;

	/* Optionally check the audio sample cadence as per SMPTE299-1:1997 table 5 page 12 */
	if (audioFrame && g_1080i2997_cadence_check && g_detected_mode_id == bmdModeHD1080i5994) {
		int depth = audioFrame->GetSampleFrameCount();
		int setlen = 5;

		/* Setup our matching table if needed, usually a one time cost */
		if (g_1080i2997_cadence_match[0] == 0) {
			memcpy(&g_1080i2997_cadence_match[0], &g_1080i2997_cadence_set[0], sizeof(g_1080i2997_cadence_set));
		}

		/* Rotate the history and the match sequence sets, then add the new cadance sample to history. */
		int m = g_1080i2997_cadence_match[0];
		for (int i = 1; i < setlen; i++) {
			g_1080i2997_cadence_hist[i - 1] = g_1080i2997_cadence_hist[i];
			g_1080i2997_cadence_match[i - 1] = g_1080i2997_cadence_match[i];
		}
		g_1080i2997_cadence_hist[setlen - 1] = depth;
		g_1080i2997_cadence_match[setlen - 1] = m;

#if 0
		static int vvv = 0;
		if (vvv++ == 100) {
			/* Damage the history just after startup to induce a control test */
			g_1080i2997_cadence_hist[2] = 1234;
		}
#endif

		/* See if we match the history and match set, meaning our history is syncronized and cadence is perfect */
		if (memcmp(&g_1080i2997_cadence_match[0], &g_1080i2997_cadence_hist[0], sizeof(g_1080i2997_cadence_hist)) == 0) {
			/* Cadence match */
		} else {

			/* Try to match the history across all five match positions, and align the match set with the measured history.
			 * The goal here is to accept that we might have valid candence in history and we need to align out match
			 * set to the candence to syncronize our candence.
			 */
			int j;
			for (j = 1; j <= setlen; j++) {
				m = g_1080i2997_cadence_match[0];
				for (int i = 1; i <= (setlen - 1); i++) {
					g_1080i2997_cadence_match[i - 1] = g_1080i2997_cadence_match[i];
				}
				g_1080i2997_cadence_match[setlen - 1] = m;
				if (memcmp(&g_1080i2997_cadence_match[0], &g_1080i2997_cadence_hist[0], sizeof(g_1080i2997_cadence_hist)) == 0) {
					printf("1080i29.97 audio cadence matched, after adjusting set\n");
					break;
				}
			}
			if (j > setlen) {
				time_t now = time(NULL);
				printf("1080i29.97 audio cadence not matched, after adjusting set:  ");
				for (int i = 0; i < setlen; i++) {
					printf("%d ", g_1080i2997_cadence_match[i]);
				}
				printf(", recd: ");
				for (int i = 0; i < setlen; i++) {
					printf("%d ", g_1080i2997_cadence_hist[i]);
				}
				printf(" @ %s", ctime(&now));
			}
		}
	}

	/* Measure any a/v offsets specific to a black/white flash pattern, if enabled. */
	if (videoFrame && audioFrame && g_bw_flash_measurements && g_bw_flash_initialized == 0) {

		int ret = bw_flash_avoffset_initialize(&g_bw_flash_ctx,
			videoFrame->GetRowBytes(),
			videoFrame->GetWidth(),
			videoFrame->GetHeight(),
			0, 				/* videoInterlaced */
			g_audioChannels,
			g_audioChannels * sizeof(uint32_t),
			0x3				/* Active Channel Bitmask, assume Stereo */ );

		if (ret == 0) {
			g_bw_flash_initialized = 1;
			printf("Initialized and counting A/V Flash pattern.\n");
		}

	}
	if (videoFrame && g_bw_flash_measurements && g_bw_flash_initialized) {
		void *frame;
		videoFrame->GetBytes(&frame);
		bw_flash_avoffset_write_video_V210(&g_bw_flash_ctx, (const unsigned char *)frame);
	}
	if (audioFrame && g_bw_flash_measurements && g_bw_flash_initialized) {
		void *frame;
		audioFrame->GetBytes(&frame);

		int complete = 0;
		bw_flash_avoffset_write_audio_s32(&g_bw_flash_ctx, (const unsigned char *)frame, audioFrame->GetSampleFrameCount(), &complete);
		if (complete) {
			bw_flash_avoffset_query_report_to_fd(&g_bw_flash_ctx, STDOUT_FILENO);
		}
	}
	/* End: Measure any a/v offsets specific to a black/white flash pattern, if enabled. */

	for (int i = 0; i < 8; i++) {
		if (g_analyzeBitmask & (1 << i)) {
			checkForSilence(audioFrame, (2 * i), g_audioChannels, g_audioSampleDepth);
			checkForSilence(audioFrame, (2 * i) + 1, g_audioChannels, g_audioSampleDepth);
		}
	}

	if (g_monitorSignalStability) {
		monitorSignal(videoFrame, audioFrame);
		return S_OK;
	}
	if (writeSession) {
		struct fwr_header_timing_s *timing;
		fwr_timing_frame_create(writeSession, (uint32_t)g_detected_mode_id, &timing);
		fwr_timing_frame_write(writeSession, timing);
		fwr_timing_frame_free(writeSession, timing);
	}
	if (muxedSession) {
		struct fwr_header_timing_s *timing;
		fwr_timing_frame_create(muxedSession, (uint32_t)g_detected_mode_id, &timing);
		fwr_writer_enqueue(muxedSession, timing, FWR_FRAME_TIMING);
	}

	IDeckLinkVideoFrame *rightEyeFrame = NULL;
	IDeckLinkVideoFrame3DExtensions *threeDExtensions = NULL;
	void *frameBytes;
	void *audioFrameBytes;
	struct frameTime_s *frameTime;

	if (g_showStartupMemory) {
		showMemory(stderr);
		g_showStartupMemory = 0;
	}

	if (muxedSession && g_muxedOutputExcludeVideo == 0 && videoFrame) {
		struct fwr_header_video_s *frame;
		videoFrame->GetBytes(&frameBytes);

		if (fwr_video_frame_create(muxedSession,
			videoFrame->GetWidth(), videoFrame->GetHeight(), videoFrame->GetRowBytes(),
			(uint8_t *)frameBytes, &frame) == 0)
		{
			fwr_writer_enqueue(muxedSession, frame, FWR_FRAME_VIDEO);
		}

	}

	// Handle Video Frame
	if (videoFrame) {

		frameTime = &frameTimes[0];

		if (g_hires_av_debug && frameTime->frameCount >= 600) {
			/* After 600 frames, start measturing statistics */

			if (frameTime->frameCount == 600) {
				const struct blackmagic_format_s *fmt = blackmagic_getFormatByMode(selectedDisplayMode);
				if (fmt == 0) {
					fprintf(stderr, "Unable to find blackmagic format, aborting.\n");
					exit(1);
				}
				hires_av_init(&g_havctx, fmt->timebase_den, fmt->timebase_num, 48000.0);
			}

			/* Queue a video frame statistically and dequeue it - because we don't transmit frames.
			 * We're measuring receive stats only.
			 */
			hires_av_rx(&g_havctx, HIRES_AV_STREAM_VIDEO, 1);
			hires_av_tx(&g_havctx, HIRES_AV_STREAM_VIDEO, 1);

			hires_av_summary_per_second(&g_havctx, 0);

			time_t now;
			time(&now);

			/* Once per minute, show the trend calculations for any drift between measured frame
			 * counts vs actual frames received.
			 */
			static time_t lastDeficit = 0;
			if (now >= (lastDeficit + 60)) {
				lastDeficit = now;

				static double counter = 0;
				counter++;
				if (counter > 1) {
					printf("Updating trend with %f\n", g_havctx.stream[HIRES_AV_STREAM_VIDEO].expected_actual_deficit_ms);
					kllineartrend_add(g_trendctx, counter, g_havctx.stream[HIRES_AV_STREAM_VIDEO].expected_actual_deficit_ms);

					kllineartrend_printf(g_trendctx);

					double slope, intersect, deviation;
					kllineartrend_calculate(g_trendctx, &slope, &intersect, &deviation);
					printf(" *******************                           Slope %15.5f Deviation is %12.2f\n", slope, deviation);
				}
			}

		}

		static int didDrop = 0;
		unsigned long long t = msecsX10();
		double interval = t - frameTime->lastTime;
		interval /= 10;
		if (frameTime->lastTime && (frameTime->lastTime + 170) < t) {
			//printf("\nLost %f frames (no frame for %7.2f ms)\n", interval / 16.7, interval);
			didDrop = 1;
		} else if (didDrop) {
			//printf("\nCatchup %4.2f ms\n", interval);
			didDrop = 0;
		}
		frameTime->lastTime = t;

		// If 3D mode is enabled we retreive the 3D extensions interface which gives.
		// us access to the right eye frame by calling GetFrameForRightEye() .
		if ((videoFrame->QueryInterface(IID_IDeckLinkVideoFrame3DExtensions, (void **)&threeDExtensions) != S_OK)
		    || (threeDExtensions->GetFrameForRightEye(&rightEyeFrame) != S_OK)) {
			rightEyeFrame = NULL;
		}

		if (threeDExtensions)
			threeDExtensions->Release();

		if (videoFrame->GetFlags() & bmdFrameHasNoInputSource) {
			g_no_signal = 1;
			if (!g_monitor_mode) {
				time_t now;
				time(&now);
				fprintf(stdout, "Frame received (#%8llu) - No input signal detected (%7.2f ms) @ %s",
					frameTime->frameCount, interval, ctime(&now));
			}
		} else {

			frameTime->frameCount++;

			g_no_signal = 0;
			char *timecodeString = NULL;
			DECKLINK_STR timecodeStringTmp = NULL;
			if (g_timecodeFormat != 0) {
				IDeckLinkTimecode *timecode;
				if (videoFrame->
				    GetTimecode(g_timecodeFormat,
						&timecode) == S_OK) {
					timecode->GetString(&timecodeStringTmp);
					timecodeString = DECKLINK_STRDUP(timecodeStringTmp);
					DECKLINK_FREE(timecodeStringTmp);
				}
			}

			unsigned int currRFC = 0;
			int isBad = 0;
#if 0
			isBad = 1;
			/* KL: Look for the framecount metadata, created by the KL signal generator. */
			unsigned int stride = videoFrame->GetRowBytes();
			unsigned char *pixelData;
			videoFrame->GetBytes((void **)&pixelData);
			pixelData += (10 * stride);
			if ((*(pixelData + 0) == 0xde) &&
			    (*(pixelData + 1) == 0xad) &&
			    (*(pixelData + 2) == 0xbe) &&
			    (*(pixelData + 3) == 0xef)) {

				unsigned char *p = pixelData + 4;

				unsigned char tag = 0;
				unsigned char taglen = 0;
				while (tag != 0xaa /* No more tags */ ) {
					tag = *p++;
					taglen = *p++;

					//fprintf(stdout, "tag %x len %x\n", tag, taglen);
					if (tag == 0x01 /* Frame counter */ ) {

						/* We need a null n the string end before we can convert it */
						unsigned char tmp[16];
						memset(tmp, 0, sizeof(tmp));
						memcpy(tmp, p, 10);

						currRFC =
						    atoi((const char *)tmp);
					}

					p += taglen;
				}

				//for (int c = 0; c < 18; c++)
				//      fprintf(stdout, "%02x ", *(pixelData + c));
				//fprintf(stdout, "\n");
			}
#endif
			if (frameTime->remoteFrameCount + 1 == currRFC)
				isBad = 0;

			if (g_verbose > 1) {
				fprintf(stdout,
					"Frame received (#%10llu) [%s] - %s - Size: %li bytes (%7.2f ms) [remoteFrame: %d] ",
					frameTime->frameCount,
					timecodeString !=
					NULL ? timecodeString : "No timecode",
					rightEyeFrame !=
					NULL ? "Valid Frame (3D left/right)" :
					"Valid Frame",
					videoFrame->GetRowBytes() *
					videoFrame->GetHeight(), interval, currRFC);
			}
			

			if (isBad) {
				fprintf(stdout, " %lld frames lost %lld->%d\n", currRFC - frameTime->remoteFrameCount,
					frameTime->remoteFrameCount, currRFC);
			}

			frameTime->remoteFrameCount = currRFC;

			if (isBad)
				showMemory(stdout);

			if (timecodeString)
				free(timecodeString);

			if (videoOutputFile != -1) {
				videoFrame->GetBytes(&frameBytes);
				write(videoOutputFile, frameBytes,
				      videoFrame->GetRowBytes() *
				      videoFrame->GetHeight());

				if (rightEyeFrame) {
					rightEyeFrame->GetBytes(&frameBytes);
					write(videoOutputFile, frameBytes,
					      videoFrame->GetRowBytes() *
					      videoFrame->GetHeight());
				}
			}
		}

		if (rightEyeFrame)
			rightEyeFrame->Release();

		if (frameTime->frameCount == 100) {
			//usleep(1100 * 1000);
		}

		if (g_maxFrames > 0 && (int)frameTime->frameCount >= g_maxFrames) {
			kill(getpid(), SIGINT);
		}
	}

	/* Video Ancillary data */
	if (videoFrame)
		ProcessVANC(videoFrame);

	if (videoFrame) {
		unsigned int stride = videoFrame->GetRowBytes();
		unsigned char *pixelData;
		videoFrame->GetBytes((void **)&pixelData);

		static uint32_t xxx = 0;
		lastGoodKLOsdCounter = V210_read_32bit_value(pixelData, stride, 1, 1);
		if (xxx + 1 != lastGoodKLOsdCounter) {
                        char t[160];
			time_t now = time(0);
                        sprintf(t, "%s", ctime(&now));
                        t[strlen(t) - 1] = 0;
			if (!g_monitor_mode)
				fprintf(stderr, "%s: KL OSD counter discontinuity, expected %08" PRIx32 " got %08" PRIx32 "\n", t, xxx + 1, lastGoodKLOsdCounter);
		}
		if (!g_monitor_mode)
			fprintf(stderr, "video counter=%d vanc counter=%d delta=%d\n", lastGoodKLOsdCounter,
				lastGoodKLFrameCounter, lastGoodKLOsdCounter - lastGoodKLFrameCounter);
		xxx = lastGoodKLOsdCounter;
	}



	// Handle Audio Frame
	if (audioFrame) {
		if (g_hires_av_debug) {
			/* Queue N audio samples statistcally and dequeue it - because we don't transmit audio.
			 * We're measuring receive stats only.
			 */
			int depth = audioFrame->GetSampleFrameCount();
			hires_av_rx(&g_havctx, HIRES_AV_STREAM_AP1, depth);
			hires_av_tx(&g_havctx, HIRES_AV_STREAM_AP1, depth);
		}

		audioFrameCount++;
		frameTime = &frameTimes[1];

		uint32_t sampleSize =
		    audioFrame->GetSampleFrameCount() * g_audioChannels *
		    (g_audioSampleDepth / 8);

		unsigned long long t = msecsX10();
		double interval = t - frameTime->lastTime;
		interval /= 10;

		if (g_verbose > 1) {
			fprintf(stdout,
				"Audio received (#%10lu) - Size: %u sfc: %lu channels: %u depth: %u bytes  (%7.2f ms)\n",
				audioFrameCount,
				sampleSize,
				audioFrame->GetSampleFrameCount(),
				g_audioChannels,
				g_audioSampleDepth / 8,
				interval);
		}

#if ENABLE_NIELSEN
		if (g_enable_nielsen && audioFrame) {
			/* We only support 32bit samples, which happens to be the klvanc_capture tool default. */
			if (g_audioSampleDepth == 32) {
				audioFrame->GetBytes(&audioFrameBytes);
				uint32_t *p = (uint32_t *)audioFrameBytes;
				/* This is a little messy, calling the API hundreds of times per buffer. Good enough for now. */
				for (int i = 0; i < audioFrame->GetSampleFrameCount(); i++) {
					for (unsigned int j = 0; j < g_audioChannels / 2; j++) {
						p++; /* Right */

						/* Left channel on Pair X */
						uint8_t *x = (uint8_t *)p;
						//fprintf(stdout, "%02x %02x %02x %02x\n", *(x + 0), *(x + 1), *(x + 2), *(x + 3));
						pNielsenAPI[j]->InputAudioData((uint8_t *)x, 4);

						p++;
					}
				}
			}
		}
#endif

		if (writeSession) {
			audioFrame->GetBytes(&audioFrameBytes);
			struct fwr_header_audio_s *frame = 0;
			if (fwr_pcm_frame_create(writeSession, audioFrame->GetSampleFrameCount(), g_audioSampleDepth, g_audioChannels, (const uint8_t *)audioFrameBytes, &frame) == 0) {
				fwr_pcm_frame_write(writeSession, frame);
				fwr_pcm_frame_free(writeSession, frame);
			}
		}

		if (muxedSession && g_muxedOutputExcludeAudio == 0) {
			audioFrame->GetBytes(&audioFrameBytes);
			struct fwr_header_audio_s *frame = 0;
			if (fwr_pcm_frame_create(muxedSession, audioFrame->GetSampleFrameCount(), g_audioSampleDepth, g_audioChannels, (const uint8_t *)audioFrameBytes, &frame) == 0) {
				fwr_writer_enqueue(muxedSession, frame, FWR_FRAME_AUDIO);
			}
		}

		frameTime->frameCount++;
		frameTime->lastTime = t;

#if HAVE_LIBKLMONITORING_KLMONITORING_H
		/* This is crying out for some refactoring and being pushed directly
		 * into libklmonitoring, but in the meantime, here's what its supposed to
		 * accomplish.
		 * a) An upstream SDI device puts PRBS15 15bit values into all of its PCM
		 *    channels. IN the buffer if uint16_t words, the buffer is prepared
		 *    as follows
		 *     for word in buffer[0 ... size]
		 *       word = next prbs15_value;
		 * b) So the entire PRBS set is stripped across all PCM channels.
		 * c) ON the receive side, we "unstripe" accross all channels, and validate
		 *    our syncronized value matches the predicted upstream value.
		 *
		 * In order for the downstream device to syncronize with upstream, it samples the
		 * last word in an initial buffer, then prepares to predict the next words for each and
		 * every subsequent buffer. If the prediction value doesn't match the actual value obtained
		 * from upstream, declare a data integrity error and re-syncronize / repeat the syncronization
		 * process.
		 */
		if (g_monitor_prbs_audio_mode) {
			audioFrame->GetBytes(&audioFrameBytes);
			if (g_prbs_initialized == 0) {
				if (g_audioSampleDepth == 16) {
					uint16_t *p = (uint16_t *)audioFrameBytes;
					for (int i = 0; i < audioFrame->GetSampleFrameCount(); i++) {
						for (int j = 0; j < g_audioChannels; j++) {
							if (i == (audioFrame->GetSampleFrameCount() - 1)) {
								if (j == (g_audioChannels - 1)) {
									printf("Seeding audio PRBS sequence with upstream value 0x%04x\n", *p);
									prbs15_init_with_seed(&g_prbs, *p);
								}
							}
							p++;
						}
					}
					g_prbs_initialized = 1;
				} else
				if (g_audioSampleDepth == 32) {
					uint32_t *p = (uint32_t *)audioFrameBytes;
					for (int i = 0; i < audioFrame->GetSampleFrameCount(); i++) {
						for (int j = 0; j < g_audioChannels; j++) {
							if (i == (audioFrame->GetSampleFrameCount() - 1)) {
								if (j == (g_audioChannels - 1)) {
									printf("Seeding audio PRBS sequence with upstream value 0x%08x\n", *p >> 16);
									prbs15_init_with_seed(&g_prbs, *p >> 16);
								}
							}
							p++;
						}
					}
					g_prbs_initialized = 1;
				} else
					assert(0);
			} else {
				if (g_audioSampleDepth == 16) {
					uint16_t *p = (uint16_t *)audioFrameBytes;
					for (int i = 0; i < audioFrame->GetSampleFrameCount(); i++) {
						for (int j = 0; j < g_audioChannels; j++) {
							uint16_t a = *p++;
							uint16_t b = prbs15_generate(&g_prbs);
							if (a != b) {
								if (g_verbose) {
									printf("%04x %04x %04x %04x -- ", *(p + 0), *(p + 1), *(p + 2), *(p + 3));
									printf("y.is:%04x pred:%04x (pos %d)\n", a, b, i);
									dumpAudio(p, audioFrame->GetSampleFrameCount(), g_audioChannels);
								}
								char t[160];
								time_t now = time(0);
								sprintf(t, "%s", ctime(&now));
								t[strlen(t) - 1] = 0;
						                fprintf(stderr, "%s: KL PRSB15 Audio frame discontinuity, expected %04" PRIx16
									" got %04" PRIx16 "\n", t, b, a);

								g_prbs_initialized = 0;

								// Break the sample frame loop i
								i = audioFrame->GetSampleFrameCount();
								break;
							}
						}
					}
				} else
				if (g_audioSampleDepth == 32) {
					uint32_t *p = (uint32_t *)audioFrameBytes;
					for (int i = 0; i < audioFrame->GetSampleFrameCount(); i++) {
						for (int j = 0; j < g_audioChannels; j++) {
							uint32_t a = *p++ >> 16;
							uint32_t b = prbs15_generate(&g_prbs);
							if (a != b) {
								if (g_verbose) {
									printf("%08x %08x %08x %08x -- ", *(p + 0), *(p + 1), *(p + 2), *(p + 3));
									printf("y.is:%04x pred:%04x (pos %d)\n", a, b, i);
									dumpAudio((uint16_t *)p, audioFrame->GetSampleFrameCount(), g_audioChannels);
								}
								char t[160];
								time_t now = time(0);
								sprintf(t, "%s", ctime(&now));
								t[strlen(t) - 1] = 0;
						                fprintf(stderr, "%s: KL PRSB15 Audio frame discontinuity, expected %08" PRIx32
									" got %08" PRIx32 "\n", t, b, a);

								g_prbs_initialized = 0;

								// Break the sample frame loop i
								i = audioFrame->GetSampleFrameCount();
								break;
							}
						}
					}
				}
			}
		}
#endif
	}
	return S_OK;
}

HRESULT DeckLinkCaptureDelegate::VideoInputFormatChanged(BMDVideoInputFormatChangedEvents events, IDeckLinkDisplayMode * mode, BMDDetectedVideoInputFormatFlags)
{
	HRESULT result;
	ltn_histogram_update_with_timevalue(hist_format_change, 1);

	if (events & bmdVideoInputDisplayModeChanged) {
		g_detected_mode_id = mode->GetDisplayMode();
		if (g_requested_mode_id == 0) {
			deckLinkInput->PauseStreams();
			result = deckLinkInput->EnableVideoInput(g_detected_mode_id,
								 g_pixelFormat, g_inputFlags);
			if (result != S_OK) {
				fprintf(stderr, "Failed to enable video input. Is another application using the card? (Result=0x%x\n", result);
			}
			deckLinkInput->FlushStreams();
			deckLinkInput->StartStreams();
		}
	}
	return S_OK;
}

/* CALLBACKS for message notification */
static int cb_AFD(void *callback_context, struct klvanc_context_s *ctx, struct klvanc_packet_afd_s *pkt)
{
	/* Have the library display some debug */
	if (!g_monitor_mode && g_verbose)
		klvanc_dump_AFD(ctx, pkt);

	return 0;
}

static int cb_EIA_708B(void *callback_context, struct klvanc_context_s *ctx, struct klvanc_packet_eia_708b_s *pkt)
{
	uint8_t caption_data[128];

	/* Have the library display some debug */
	if (!g_monitor_mode && g_verbose)
		klvanc_dump_EIA_708B(ctx, pkt);

	if (rcwtOutputFile >= 0) {
		if (pkt->ccdata.cc_count * 3 > sizeof(caption_data))
			return -1;
		for (size_t i = 0; i < pkt->ccdata.cc_count; i++) {
			caption_data[3*i+0] =  0xf8 | (pkt->ccdata.cc[i].cc_valid ? 0x04 : 0x00) |
				(pkt->ccdata.cc[i].cc_type & 0x03);
			caption_data[3*i+1] = pkt->ccdata.cc[i].cc_data[0];
			caption_data[3*i+2] = pkt->ccdata.cc[i].cc_data[1];
		}
		/* RCWT format expects time in millseconds, relative to start of file */
		struct timeval diff;
		if (ftfirst.ts1.tv_sec == 0 && ftfirst.ts1.tv_usec == 0)
			ftfirst = ftlast;
		fwr_timeval_subtract(&diff, &ftlast.ts1, &ftfirst.ts1);

		rcwt_write_captions(rcwtOutputFile, pkt->ccdata.cc_count, caption_data,
				    (diff.tv_sec * 1000000 + diff.tv_usec) / 1000);
	}

	return 0;
}

static int cb_EIA_608(void *callback_context, struct klvanc_context_s *ctx, struct klvanc_packet_eia_608_s *pkt)
{
	/* Have the library display some debug */
	if (!g_monitor_mode && g_verbose)
		klvanc_dump_EIA_608(ctx, pkt);

	return 0;
}

static int cb_SCTE_104(void *callback_context, struct klvanc_context_s *ctx, struct klvanc_packet_scte_104_s *pkt)
{
	int ret;

	/* Have the library display some debug */
	if (!g_monitor_mode && g_verbose) {
		ret = klvanc_dump_SCTE_104(ctx, pkt);
		if (ret != 0)
			fprintf(stderr, "Error dumping SCTE 104 packet!\n");
	}

	return 0;
}

static int cb_SDP(void *callback_context, struct klvanc_context_s *ctx, struct klvanc_packet_sdp_s *pkt)
{
	/* Have the library display some debug */
	if (!g_monitor_mode && g_verbose)
		klvanc_dump_SDP(ctx, pkt);

	return 0;
}

static int cb_SMPTE_12_2(void *callback_context, struct klvanc_context_s *ctx, struct klvanc_packet_smpte_12_2_s *pkt)
{
	/* Have the library display some debug */
	if (!g_monitor_mode && g_verbose)
		klvanc_dump_SMPTE_12_2(ctx, pkt);

	return 0;
}

static int cb_SMPTE_2108_1(void *callback_context, struct klvanc_context_s *ctx, struct klvanc_packet_smpte_2108_1_s *pkt)
{
	/* Have the library display some debug */
	if (!g_monitor_mode && g_verbose)
		klvanc_dump_SMPTE_2108_1(ctx, pkt);

	return 0;
}

static int cb_all(void *callback_context, struct klvanc_context_s *ctx, struct klvanc_packet_header_s *pkt)
{
	/* Save the packet to disk, if reqd. */
	if (g_vancOutputDir) {
		int requestedLine = g_linenr;
		if (requestedLine == 0)
			requestedLine = -1; /* All lines */

		klvanc_packet_save(g_vancOutputDir,
			(const struct klvanc_packet_header_s *)pkt,
			requestedLine,
			-1 /* did */);
	}

#if HAVE_CURSES_H
#if 0
	vanc_monitor_update(ctx, pkt, &selected);
#endif
#endif

	if (g_packetizeSMPTE2038) {
		if (klvanc_smpte2038_packetizer_append(smpte2038_ctx, pkt) < 0) {
		}
	}

	return 0;
}

static int cb_VANC_TYPE_KL_UINT64_COUNTER(void *callback_context, struct klvanc_context_s *ctx, struct klvanc_packet_kl_u64le_counter_s *pkt)
{
	/* Have the library display some debug */
	if (!g_monitor_mode && g_verbose)
		klvanc_dump_KL_U64LE_COUNTER(ctx, pkt);

	if (lastGoodKLFrameCounter && lastGoodKLFrameCounter + 1 != pkt->counter) {
		char t[160];
		time_t now = time(0);
		sprintf(t, "%s", ctime(&now));
		t[strlen(t) - 1] = 0;

		fprintf(stderr, "%s: KL VANC frame counter discontinuity was %" PRIu64 " now %" PRIu64 "\n",
			t,
			lastGoodKLFrameCounter, pkt->counter);
	}
	lastGoodKLFrameCounter = pkt->counter;

	return 0;
}

static struct klvanc_callbacks_s callbacks =
{
	.afd                    = cb_AFD,
	.eia_708b               = cb_EIA_708B,
	.eia_608                = cb_EIA_608,
	.scte_104               = cb_SCTE_104,
	.all                    = cb_all,
	.kl_i64le_counter       = cb_VANC_TYPE_KL_UINT64_COUNTER,
	.sdp                    = cb_SDP,
	.smpte_12_2             = cb_SMPTE_12_2,
	.smpte_2108_1           = cb_SMPTE_2108_1,
};

/* END - CALLBACKS for message notification */

static void listDisplayModes()
{
	int displayModeCount = 0;
	IDeckLinkDisplayMode *displayMode;
	while (displayModeIterator->Next(&displayMode) == S_OK) {

		char * displayModeString = NULL;
		DECKLINK_STR displayModeStringTmp = NULL;
		HRESULT result = displayMode->GetName(&displayModeStringTmp);
		if (result == S_OK) {
			BMDTimeValue frameRateDuration, frameRateScale;
			displayMode->GetFrameRate(&frameRateDuration, &frameRateScale);
			displayModeString = DECKLINK_STRDUP(displayModeStringTmp);
			DECKLINK_FREE(displayModeStringTmp);
			fprintf(stderr, "        %c%c%c%c : %-20s \t %li x %li \t %7g FPS\n",
				displayMode->GetDisplayMode() >> 24,
				displayMode->GetDisplayMode() >> 16,
				displayMode->GetDisplayMode() >>  8,
				displayMode->GetDisplayMode(),
				displayModeString,
				displayMode->GetWidth(),
				displayMode->GetHeight(),
				(double)frameRateScale /
				(double)frameRateDuration);

			free(displayModeString);
			displayModeCount++;
		}

		displayMode->Release();
	}
}

static int usage(const char *progname, int status)
{
	fprintf(stderr, COPYRIGHT "\n");
	fprintf(stderr, "Capture decklink SDI payload, capture vanc, analyze vanc.\n");
	fprintf(stderr, "Version: " GIT_VERSION "\n");
	fprintf(stderr, "Usage: %s [OPTIONS]\n", basename((char *)progname));
	fprintf(stderr,
		"    -p <pixelformat>\n"
		"         0:   8 bit YUV (4:2:2)\n"
		"         1:  10 bit YUV (4:2:2) (def)\n"
		"         2:  10 bit RGB (4:4:4)\n"
		"    -t <format> Print timecode\n"
		"        rp188:  RP 188\n"
		"         vitc:  VITC\n"
		"       serial:  Serial Timecode\n"
		"    -f <filename>   raw video output filename (DEPRECATED - Use -x instead)\n"
		"    -a <filename>   raw audio output filaname\n"
		"    -A <filename>   Attempt to detect SMPTE337 on the audio file payload, extract payload into pair files,\n"
		"                    inspect audio buffers at a byte level. Input should be a file created with -a.\n"
		"    -B              Monitor A/V offsets from a white flash to the pulse tone.\n"
		"    -V <filename>   raw vanc output filename\n"
		"    -I <filename>   Interpret and display input VANC filename (See -V)\n"
		"    -R <filename>   RCWT caption output filename\n"
		"    -k              Enable analysis of KL frame counters in video and VANC\n"
		"    -l <linenr>     During -I parse, process a specific line# (def: 0 all)\n"
		"    -L              List available display modes\n"
		"    -m <mode>       Force to capture in specified mode\n"
		"                    Eg. Hi59 (1080i59), hp60 (1280x720p60) Hp60 (1080p60) (def: ntsc):\n"
		"                    See -L for a complete list of which modes are supported by the capture hardware.\n"
		"                    Device will autodetect format if not specified.  If mode is specified, capture\n"
		"                    will be locked to that mode and 'no signal' will be reported if not matching.\n"
		"    -c <channels>   Audio Channels (2, 8 or 16 - def: %d)\n"
		"    -s <depth>      Audio Sample Depth (16 or 32 - def: %d)\n"
		"    -n <frames>     Number of frames to capture (def: unlimited)\n"
		"    -v              Increase level of verbosity (def: 0)\n"
		"    -3              Capture Stereoscopic 3D (Requires 3D Hardware support)\n"
		"    -9              Check for SMPTE-299M-1 1080i29.97 audio frame cadence (console only)\n"
		"    -i <number>     Capture from input port (def: 0)\n"
		"    -P pid 0xNNNN   Packetsize all detected VANC into SMPTE2038 TS packets using pid.\n"
		"                    The packets are store in file %s\n"
#if HAVE_CURSES_H
		"    -M              During VANC capture, display a Curses onscreen UI.\n"
#endif
#if ENABLE_NIELSEN
		"    -N              During live capture, for all audio pairs (left ch only), enable Nielsen code extraction to console.\n"
		"                    *** Does not work with bitstream audio, regular PCM audio only ***\n"
#endif
#if HAVE_LIBKLMONITORING_KLMONITORING_H
		"    -S              Validate PRBS15 sequences are correct on all audio channels (def: disabled).\n"
#endif
		"    -x <filename>   Create a muxed audio+video+vanc output file.\n"
		"    -ev             Exclude video from muxed output file.\n"
		"    -ea             Exclude audio from muxed output file.\n"
		"    -ed             Exclude data (vanc) from muxed output file.\n"
		"    -X <filename>   Analyze a muxed audio+video+vanc input file.\n"
		"    -Z <pair# 1-8>  Check for audio silence on the given audio pairs.\n"
		"    -K <number>     audio samples ceiling before tripping silence alert (-Z). (def: 24)\n"
		"    -T <dirname>    Save all vanc messages into dirname as a seperate unique file (16bit words).\n"
		"    -Y <seconds>    Monitor SDK callback intervals and report to console periodically.\n"
		"    -H              Monitor frame arrival intervals, attempt to measure SDI inputs that run less than realtime\n"
		"                    Make sure you specify -m and force the video mode when using this feature\n"
		"\n"
		"Capture raw video and audio to file then playback. 1920x1080p30, 50 complete frames, PCM audio, 8bit mode:\n"
		"    %s -mHp30 -n 50 -f video.raw -a audio.raw -p0\n"
		"    mplayer video.raw -demuxer rawvideo -rawvideo fps=30:w=1920:h=1080:format=uyvy \\\n"
		"        -audiofile audio.raw -audio-demuxer 20 -rawaudio rate=48000\n\n",
		g_audioChannels,
		g_audioSampleDepth,
		TS_OUTPUT_NAME,
		basename((char *)progname)
		);

	fprintf(stderr, "Use cases:\n"
		"1) Capture audio only to disk, view the data in hex format, convert the data into 2-channel pairs, convert a pair into .wav for playback:\n"
		"\t1a) Capture only raw audio data from 1280x720p60.\n"
		"\t\t-a audio.raw -mhp60\n"
		"\t1b) Visually inspect the captured audio file, and deinterleave/extract audio into new 'pair[0-7].raw' files.\n"
		"\t\t-A audio.raw\n"
		"\t1c) Convert a 'pair[0-7].raw' file into a playable wav file.\n"
		"\t\tffmpeg -y -f s32le -ar 48k -ac 2 -i <pair.raw file> output.wav\n"
		"2) Display all VANC messages onscreen in an interactive UI (1080i 59.94), (10bit incoming video):\n"
    		"\t-mHi59 -p1 -M\n"
		"3) Capture VANC data to disk for offline inspection, then inspect it. (1080p60 10bit incoming video):\n"
		"\t3a) Capture VANC data to disk (1080p60 10bit incoming video):\n"
    		"\t\t-mHp60 -p1 -V vanc.raw\n"
		"\t3b) Parse/Interpret the offline VANC file, show any vanc data:\n"
		"\t\t-I vanc.raw -v\n"
		"4) Capture 300 frames of video for playback with mplayer, then play it back 1080p30 (8bit support only):\n"
		"   The resulting file will be huge, so typically you might only want to do this for 5-30 seconds.\n"
		"\t4a) Capture video:\n"
		"\t\t-mHp30 -n300 -f video.raw -p0\n"
		"\t4b) Playback video:\n"
		"\t\tmplayer video.raw -demuxer rawvideo -rawvideo fps=30:w=1920:h=1080:format=uyvy\n"
		"5) Capture audio, video and VANC to a single file for offline inspection 1280x720p60 (10bit):\n"
		"   The resulting file will be huge, so typically you might only want to do this for 5-30 seconds.\n"
		"\t5a) Capture the signal to disk:\n"
		"\t\t-mhp60 -p1 -x capture.mx\n"
		"\t5b) Inspect a previously captured mx file (WORK IN PROGRESS):\n"
		"\t\t-X capture.mx\n"
		"6) Capture only VANC to a muxed single file for offline inspection 1280x720p59.94 (10bit):\n"
		"\t6a) Capture the signal to disk, discarding audio and video (platform endian format):\n"
		"\t\t-mhp59 -p1 -x capture.mx -ea -ev\n"
		"\t6b) Inspect a previously captured file, save VANC packets to /tmp/mypackets dir:\n"
		"\t\t-X capture.mx -T /tmp/mypackets\n"
		"\t6c) Only save VANC packet on line 13 to dir /tmp/mypackets:\n"
		"\t\t-X capture.mx -T /tmp/mypackets -l 13\n"
#if ENABLE_NIELSEN
		"7) Analyze signal for Nielsen codes. Displays a JSON summary every 60 seconds:\n"
		"\t7a) For a 1280x720p59.94 input signal, anaylyze all pairs all channels 0-15:\n"
		"\t    Make sure you specifiy 16 audio channels and 32bit depth (they're the current defaults):\n"
		"\t\t -mhp59 -N\n"
#endif
		"8) Some SDI output devices drop audio samples if they are internally reset or have internal processing errors.\n"
		"   Assuming the input signal is a constant tone, we can detect loss by checking for PCM with no credible\n"
		"   audio waveform on the first two audio pairs, and the fourth pair.\n"
		"\t\t-i0 -mhp59 -c16 -s32 -Z1 -Z2 -Z4\n"
		"9) Decode SCTE104 from 1080p59.94, input 3, messages to console (super chatty with other messages too).\n"
		"\t\t-i3 -mHp59 -v\n"
		"10) Check 1080i29.97 (specifically) audio cadences are within SDI sped SMPTE-299, messages to console when errors detected.\n"
		"\t\t-i3 -mHi59 -9\n"

	);

	exit(status);
}

static int _main(int argc, char *argv[])
{
	IDeckLinkIterator *deckLinkIterator = CreateDeckLinkIteratorInstance();
	DeckLinkCaptureDelegate *delegate;

	int exitStatus = 1;
	int ch;
	int portnr = 0;
	bool wantHelp = false;
	bool wantDisplayModes = false;
	HRESULT result;

	pthread_mutex_init(&sleepMutex, NULL);
	pthread_cond_init(&sleepCond, NULL);

	ltn_histogram_alloc_video_defaults(&hist_arrival_interval, "A/V arrival intervals");
	ltn_histogram_alloc_video_defaults(&hist_arrival_interval_video, "video arrival intervals");
	ltn_histogram_alloc_video_defaults(&hist_arrival_interval_audio, "audio arrival intervals");
	ltn_histogram_alloc_video_defaults(&hist_audio_sfc, "audio sfc");
	ltn_histogram_alloc_video_defaults(&hist_format_change, "video format change");
	memset(&g_asctx, 0, sizeof(g_asctx));

	int v;
	while ((ch = getopt(argc, argv, "?h39c:s:f:a:A:Bm:n:p:t:vV:HI:i:K:l:LP:MNSx:X:R:e:T:Y:Z:k")) != -1) {
		switch (ch) {
		case '9':
			g_1080i2997_cadence_check = 1;
			break;
#if HAVE_LIBKLMONITORING_KLMONITORING_H
		case 'S':
			g_monitor_prbs_audio_mode = 1;
			g_prbs_initialized = 0;
			break;
#endif
		case 'K':
			g_silencemax = atoi(optarg);
			break;
		case 'm':
			selectedDisplayMode  = *(optarg + 0) << 24;
			selectedDisplayMode |= *(optarg + 1) << 16;
			selectedDisplayMode |= *(optarg + 2) <<  8;
			selectedDisplayMode |= *(optarg + 3);
			g_requested_mode_id = selectedDisplayMode;
			g_detected_mode_id = selectedDisplayMode;
			break;
		case 'x':
			g_muxedOutputFilename = optarg;
			break;
		case 'X':
			g_muxedInputFilename = optarg;
			break;
		case 'e':
			switch (optarg[0]) {
			case 'v':
				g_muxedOutputExcludeVideo = 1;
				break;
			case 'a':
				g_muxedOutputExcludeAudio = 1;
				break;
			case 'd':
				g_muxedOutputExcludeData = 1;
				break;
			default:
				fprintf(stderr, "Only valid types to exclude are video/audio/data\n");
				goto bail;
			}
			break;
		case 'c':
			g_audioChannels = atoi(optarg);
			if (g_audioChannels != 2 && g_audioChannels != 8 && g_audioChannels != 16) {
				fprintf(stderr, "Invalid argument: Audio Channels must be either 2, 8 or 16\n");
				goto bail;
			}
			break;
		case 's':
			g_audioSampleDepth = atoi(optarg);
			if (g_audioSampleDepth != 16 && g_audioSampleDepth != 32) {
				fprintf(stderr, "Invalid argument: Audio Sample Depth must be either 16 bits or 32 bits\n");
				goto bail;
			}
			break;
		case 'f':
			g_videoOutputFilename = optarg;
			break;
		case 'a':
			g_audioOutputFilename = optarg;
			break;
		case 'A':
			g_enable_smpte337_detector = 1;
			g_audioInputFilename = optarg;
			break;
		case 'B':
			g_bw_flash_measurements = 1;
			break;
		case 'H':
			g_hires_av_debug = 1;
			g_trendctx = kllineartrend_alloc(60 * 60 * 60, "Video Drift Trend");
			break;
		case 'I':
			g_vancInputFilename = optarg;
			break;
		case 'i':
			portnr = atoi(optarg);
			break;
		case 'l':
			g_linenr = atoi(optarg);
			break;
		case 'L':
			wantDisplayModes = true;
			break;
		case 'V':
			g_vancOutputFilename = optarg;
			break;
		case 'R':
			g_rcwtOutputFilename = optarg;
			break;
		case 'Y':
			g_monitorSignalStability = 1;
			g_hist_print_interval = atoi(optarg);
			break;
		case 'n':
			g_maxFrames = atoi(optarg);
			break;
#if HAVE_CURSES_H
		case 'M':
			g_monitor_mode = 1;
			break;
#endif
#if ENABLE_NIELSEN
		case 'N':
			g_enable_nielsen = 1;
			break;
#endif
		case 'v':
			g_verbose++;
			break;
		case '3':
			g_inputFlags |= bmdVideoInputDualStream3D;
			break;
		case 'p':
			switch (atoi(optarg)) {
			case 0:
				g_pixelFormat = bmdFormat8BitYUV;
				break;
			case 1:
				g_pixelFormat = bmdFormat10BitYUV;
				break;
			case 2:
				g_pixelFormat = bmdFormat10BitRGB;
				break;
			default:
				fprintf(stderr, "Invalid argument: Pixel format %d is not valid", atoi(optarg));
				goto bail;
			}
			break;
		case 't':
			if (!strcmp(optarg, "rp188"))
				g_timecodeFormat = bmdTimecodeRP188Any;
			else if (!strcmp(optarg, "vitc"))
				g_timecodeFormat = bmdTimecodeVITC;
			else if (!strcmp(optarg, "serial"))
				g_timecodeFormat = bmdTimecodeSerial;
			else {
				fprintf(stderr, "Invalid argument: Timecode format \"%s\" is invalid\n", optarg);
				goto bail;
			}
			break;
		case 'P':
			g_packetizeSMPTE2038 = 1;
			if ((sscanf(optarg, "0x%x", &g_packetizePID) != 1) || (g_packetizePID > 0x1fff)) {
				wantHelp = true;
			} else {
				/* Success */
			}
			break;
		case 'T':
			g_vancOutputDir = strdup(optarg);
			break;
		case 'Z':
			v = atoi(optarg);
			if (v < 1 || v > 8) {
				fprintf(stderr, "Invalid argument for Z '%s': Valid values 1-8\n", optarg);
				goto bail;
			}
			g_analyzeBitmask |= (1 << (v - 1));
			break;
		case 'k':
			g_kl_osd_vanc_compare = 1;
			break;

		case '?':
		case 'h':
			wantHelp = true;
		}
	}

	if (wantHelp) {
		usage(argv[0], 0);
		goto bail;
	}

#if ENABLE_NIELSEN
	if (g_enable_nielsen) {
		for (unsigned int i = 0; i < g_audioChannels / 2; i++) {
			pNielsenParams[i] = new CMonitorSdkParameters();
			pNielsenParams[i]->SetSampleSize(32);
			pNielsenParams[i]->SetPackingMode(FourBytesMsbPadding);
			pNielsenParams[i]->SetSampleRate(48000);
			if (pNielsenParams[i]->ValidateAllSettings() != 1) {
				fprintf(stderr, "Error validating nielsen parameters for pair %d, aborting.\n", i);
				exit(0);
			}

			pNielsenCallback[i] = new CMonitorSdkCallback(i);
			pNielsenAPI[i] = new CMonitorApi(pNielsenParams[i], pNielsenCallback[i]);
			pNielsenAPI[i]->SetIncludeDetailedReport(1);
			pNielsenAPI[i]->Initialize();
			if (pNielsenAPI[i]->IsProcessorInitialized() != 1) {
				fprintf(stderr, "Error initializing nielsen decoder for pair %d, aborting.\n", i);
				exit(0);
			}
		}
	}
#endif

	if (g_rcwtOutputFilename != NULL) {
		rcwtOutputFile = open(g_rcwtOutputFilename, O_WRONLY | O_CREAT | O_TRUNC, 0664);
		if (rcwtOutputFile < 0) {
			fprintf(stderr, "Could not open rcwt output file \"%s\"\n", g_rcwtOutputFilename);
			goto bail;
		}
		if (rcwt_write_header(rcwtOutputFile, 0xcc, 0x0052) < 0) {
			fprintf(stderr, "Could not write rcwt header to output file\n");
			goto bail;
		}
	}

 	if (g_packetizeSMPTE2038) {
		unlink(TS_OUTPUT_NAME);
		if (klvanc_smpte2038_packetizer_alloc(&smpte2038_ctx) < 0) {
			fprintf(stderr, "Unable to allocate a SMPTE2038 context.\n");
			goto bail;
		}
	}

        if (klvanc_context_create(&vanchdl) < 0) {
                fprintf(stderr, "Error initializing library context\n");
                exit(1);
        }

	if (g_monitor_mode)
		klvanc_context_enable_cache(vanchdl);

	/* We specifically want to see packets that have bad checksums. */
	vanchdl->allow_bad_checksums = 1;
	vanchdl->warn_on_decode_failure = 1;
	vanchdl->verbose = g_verbose;
	vanchdl->callbacks = &callbacks;

	if (g_vancInputFilename != NULL) {
		return AnalyzeVANC(g_vancInputFilename);
	}

	if (g_audioInputFilename != NULL) {
		return AnalyzeAudio(g_audioInputFilename);
	}
	if (g_muxedInputFilename != NULL) {
		return AnalyzeMuxed(g_muxedInputFilename);
	}

	if (!deckLinkIterator) {
		fprintf(stderr, "This application requires the DeckLink drivers installed.\n");
		goto bail;
	}

	for (int i = 0; i <= portnr; i++) {
		/* Connect to the nth DeckLink instance */
		result = deckLinkIterator->Next(&deckLink);
		if (result != S_OK) {
			fprintf(stderr, "No capture devices found.\n");
			goto bail;
		}
	}

	if (deckLink->QueryInterface(IID_IDeckLinkInput, (void **)&deckLinkInput) != S_OK) {
		fprintf(stderr, "No input capture devices found.\n");
		goto bail;
	}

	delegate = new DeckLinkCaptureDelegate();
	deckLinkInput->SetCallback(delegate);

	/* Obtain an IDeckLinkDisplayModeIterator to enumerate the display modes supported on output */
	result = deckLinkInput->GetDisplayModeIterator(&displayModeIterator);
	if (result != S_OK) {
		fprintf(stderr, "Could not obtain the video output display mode iterator - result = %08x\n", result);
		goto bail;
	}

	if (wantDisplayModes) {
		listDisplayModes();
		goto bail;
	}

	if (g_videoOutputFilename != NULL) {
		videoOutputFile = open(g_videoOutputFilename, O_WRONLY | O_CREAT | O_TRUNC, 0664);
		if (videoOutputFile < 0) {
			fprintf(stderr, "Could not open video output file \"%s\"\n", g_videoOutputFilename);
			goto bail;
		}
	}
	if (g_muxedOutputFilename != NULL) {
		if (fwr_session_file_open(g_muxedOutputFilename, 1, &muxedSession) < 0) {
			fprintf(stderr, "Could not open muxed output file \"%s\"\n", g_muxedOutputFilename);
			goto bail;
		}
	}

	if (g_audioOutputFilename != NULL) {
		if (fwr_session_file_open(g_audioOutputFilename, 1, &writeSession) < 0) {
			fprintf(stderr, "Could not open audio output file \"%s\"\n", g_audioOutputFilename);
			goto bail;
		}
	}

	if (g_vancOutputFilename != NULL) {
		vancOutputFile = open(g_vancOutputFilename, O_WRONLY | O_CREAT | O_TRUNC, 0664);
		if (vancOutputFile < 0) {
			fprintf(stderr, "Could not open vanc output file \"%s\"\n", g_vancOutputFilename);
			goto bail;
		}
	}

	/* Confirm the users requested display mode and other settings are valid for this device. */
	BMDDisplayModeSupport dm;
	deckLinkInput->DoesSupportVideoMode(selectedDisplayMode, g_pixelFormat, g_inputFlags, &dm, NULL);
	if (dm == bmdDisplayModeNotSupported) {
		fprintf(stderr, "The requested display mode is not supported with the selected pixel format\n");
		goto bail;
	}

	result = deckLinkInput->EnableVideoInput(selectedDisplayMode, g_pixelFormat, g_inputFlags);
	if (result != S_OK) {
		fprintf(stderr, "Failed to enable video input. Is another application using the card?\n");
		goto bail;
	}

	result = deckLinkInput->EnableAudioInput(bmdAudioSampleRate48kHz, g_audioSampleDepth, g_audioChannels);
	if (result != S_OK) {
		fprintf(stderr, "Failed to enable audio input. Is another application using the card?\n");
		goto bail;
	}

	result = deckLinkInput->StartStreams();
	if (result != S_OK) {
		fprintf(stderr, "Failed to start stream. Is another application using the card?\n");
		goto bail;
	}

	signal(SIGINT, signal_handler);
	signal(SIGUSR1, signal_handler);
	signal(SIGUSR2, signal_handler);

#if HAVE_CURSES_H
	if (g_monitor_mode) {
		initscr();
		pthread_create(&g_monitor_draw_threadId, 0, thread_func_draw, NULL);
		pthread_create(&g_monitor_input_threadId, 0, thread_func_input, NULL);
	}
#endif

	/* All Okay. */
	exitStatus = 0;

	/* Block main thread until signal occurs */
	pthread_mutex_lock(&sleepMutex);
	while (g_shutdown == 0)
		pthread_cond_wait(&sleepCond, &sleepMutex);
	pthread_mutex_unlock(&sleepMutex);

	while (g_shutdown != 2)
		usleep(50 * 1000);

	fprintf(stdout, "Stopping Capture\n");
	result = deckLinkInput->StopStreams();
	if (result != S_OK) {
		fprintf(stderr, "Failed to start stream. Is another application using the card?\n");
	}

#if HAVE_CURSES_H
	vanc_monitor_stats_dump();
#endif
        klvanc_context_destroy(vanchdl);
	klvanc_smpte2038_packetizer_free(&smpte2038_ctx);

#if HAVE_CURSES_H
	if (g_monitor_mode)
		endwin();
#endif

#if ENABLE_NIELSEN
	for (unsigned int i = 0; i < g_audioChannels / 2; i++) {
		delete pNielsenAPI[i];
		delete pNielsenCallback[i];
		delete pNielsenParams[i];
	}
#endif

bail:

	if (videoOutputFile)
		close(videoOutputFile);
	if (audioOutputFile)
		close(audioOutputFile);
	if (writeSession)
		fwr_session_file_close(writeSession);
	if (muxedSession)
		fwr_session_file_close(muxedSession);
	if (vancOutputFile)
		close(vancOutputFile);
	if (rcwtOutputFile)
		close(rcwtOutputFile);

	RELEASE_IF_NOT_NULL(displayModeIterator);
	RELEASE_IF_NOT_NULL(deckLinkInput);
	RELEASE_IF_NOT_NULL(deckLink);
	RELEASE_IF_NOT_NULL(deckLinkIterator);

	ltn_histogram_free(hist_arrival_interval);
	ltn_histogram_free(hist_arrival_interval_video);
	ltn_histogram_free(hist_arrival_interval_audio);
	ltn_histogram_free(hist_audio_sfc);
	ltn_histogram_free(hist_format_change);

	return exitStatus;
}

extern "C" int capture_main(int argc, char *argv[])
{
	return _main(argc, argv);
}

