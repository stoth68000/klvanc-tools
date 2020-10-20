
AUTOMAKE_OPTIONS = foreign
GIT_VERSION := $(shell git describe --abbrev=8 --dirty --always --tags)

AM_CPPFLAGS = -DVERSION=\"$(VERSION)\" -DPROG="\"$(PACKAGE)\"" -D_FILE_OFFSET_BITS=64 \
	-D_BSD_SOURCE \
	-I$(BLACKMAGIC_SDK_PATH) \
	-I$(top_srcdir)/src \
	-DGIT_VERSION=\"$(GIT_VERSION)\" \
	-DKL_USERSPACE -D__STDC_FORMAT_MACROS $(ZLIB_CFLAGS) -std=c++11

AM_CFLAGS = -Wall -O3

LDADD = -lpthread -lz -ldl -lklvanc $(ZLIB_LIBS)

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
SRC += rcwt.c
SRC += nielsen.cpp
SRC += Config.cpp db.cpp transmitter.cpp

#bin_PROGRAMS  = klvanc_util
bin_PROGRAMS  = klvanc_capture klvanc_transmitter

#klvanc_util_SOURCES = $(SRC)
klvanc_capture_SOURCES = $(SRC)
klvanc_transmitter_SOURCES = $(SRC)

libklvanc_noinst_includedir = $(includedir)

noinst_HEADERS  = hexdump.h
noinst_HEADERS += version.h
noinst_HEADERS += nielsen.h
