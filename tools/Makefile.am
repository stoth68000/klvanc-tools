
AUTOMAKE_OPTIONS = foreign

BLACKMAGIC_SDK_PATH = $(top_srcdir)/include/decklink-sdk

AM_CPPFLAGS = -DVERSION=\"$(VERSION)\" -DPROG="\"$(PACKAGE)\"" -D_FILE_OFFSET_BITS=64 \
	-D_BSD_SOURCE \
	-I$(BLACKMAGIC_SDK_PATH) \
	-I$(top_srcdir)/src \
	-DKL_USERSPACE -D__STDC_FORMAT_MACROS

AM_CFLAGS = -Wall -O3

LDADD = -lpthread -lz -ldl -lklvanc

if DEBUG
	CFLAGS += -g
endif

CXXFLAGS = $(CFLAGS)

SRC  = klvanc_util.c
SRC += $(BLACKMAGIC_SDK_PATH)//DeckLinkAPIDispatch.cpp
SRC += capture.cpp
SRC += ts_packetizer.c
SRC += klringbuffer.c
SRC += frame-writer.c
SRC += smpte337_detector.c

#bin_PROGRAMS  = klvanc_util
bin_PROGRAMS  = klvanc_capture

#klvanc_util_SOURCES = $(SRC)
klvanc_capture_SOURCES = $(SRC)

libklvanc_noinst_includedir = $(includedir)

noinst_HEADERS  = hexdump.h
noinst_HEADERS += version.h
