/*
 * Copyright (c) 2018 Kernel Labs Inc. All Rights Reserved
 *
 * Address: Kernel Labs Inc., PO Box 745, St James, NY. 11780
 * Contact: sales@kernellabs.com
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

/**
 * @file	rcwt.h
 * @author	Devin Heitmueller <dheitmueller@kernellabs.com>
 * @copyright	Copyright (c) 2018 Kernel Labs Inc. All Rights Reserved.
 * @brief	Helper functions to create files readable by CCExtractor
 */

/* Raw Captions With Time (RCWT).  Defined as the binary file format
 * of CCExtractor, starting in version 0.52.  The full text of the
 * specification can be found in the CCExtractor source tarball in
 * the file named docs/BINARY_FILE_FORMAT.TXT
 */

#ifndef RCWT_H
#define RCWT_H

#include <stdlib.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int rcwt_write_header(int fd, uint8_t creating_program, uint16_t program_version);
int rcwt_write_captions(int fd, uint16_t cc_count, uint8_t *caption_data, uint64_t caption_time);

#ifdef __cplusplus
};
#endif

#endif /* RWCT_H */
