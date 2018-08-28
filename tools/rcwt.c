/* Copyright (c) 2018 Kernel Labs Inc. All Rights Reserved. */

#include "rcwt.h"
#include <stdio.h>
#include <unistd.h>
#include <string.h>

int rcwt_create_header(int fd, uint8_t creating_program, uint16_t program_version)
{
	char header[11];
	ssize_t ret;

	/* Magic number */
	header[0] = 0xcc;
	header[1] = 0xcc;
	header[2] = 0xed;

	/* Creating Program */
	header[3] = creating_program;

	/* Program version number */
	header[4] = program_version >> 8;
	header[5] = program_version & 0xff;

	/* File format version */
	header[6] = 0x00;
	header[7] = 0x01;
	/* Reserved */
	header[8] = 0x00;
	header[9] = 0x00;
	header[10] = 0x00;
	ret = write(fd, header, sizeof(header));
	if (ret != sizeof(header))
		return -1;
	return 0;
}

int rcwt_write_captions(int fd, uint16_t cc_count, uint8_t *caption_data, uint64_t caption_time)
{
	int ret;
	uint8_t group_header[10];

	/* Specification doesn't explicitly indicate Endianness,
	   but CCExtractor doesn't make any effort to do byte order
	   conversion and Intel is the most common platform */
	group_header[0] = caption_time       & 0xff;
	group_header[1] = caption_time >>  8 & 0xff;
	group_header[2] = caption_time >> 16 & 0xff;
	group_header[3] = caption_time >> 24 & 0xff;
	group_header[4] = caption_time >> 32 & 0xff;
	group_header[5] = caption_time >> 40 & 0xff;
	group_header[6] = caption_time >> 48 & 0xff;
	group_header[7] = caption_time >> 56 & 0xff;

	group_header[8] = cc_count      & 0xff;
	group_header[9] = cc_count >> 8 & 0xff;

	/* FIXME: endianness for caption_time/FTS values? */
	ret = write(fd, group_header, sizeof(group_header));
	if (ret != sizeof(group_header))
		return -1;
	ret = write(fd, caption_data, cc_count * 3);
	if (ret != sizeof(cc_count * 3))
		return -1;

	return 0;
}
