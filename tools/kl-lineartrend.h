/**
 * @file	kl-lineartrend.h
 * @author	Steven Toth <stoth@kernellabs.com>
 * @copyright	Copyright (c) 2020 Kernel Labs Inc. All Rights Reserved.
 * The source for this lives in libklmonitoring. Make sure any local change patches
 * are pushed upstream.
 */

#ifndef KL_LINEARTREND_H
#define KL_LINEARTREND_H

#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>

#ifdef __cplusplus
extern "C"
{
#endif

struct kllineartrend_item_s
{
	double x;
	double y;
};

struct kllineartrend_context_s
{
	char name[128];

	int idx;
	int count;
	int maxCount;
	struct kllineartrend_item_s *list;

};

/**
 * @brief	Allocate a context.
 * @return   	Pointer on success, else NULL.
 */
struct kllineartrend_context_s *kllineartrend_alloc(uint32_t maxItems, const char *name);

/**
 * @brief	Release and de-allocate any memory resources associated with object.
 * @param[in]	struct kllineartrend_context_s *ctx - Object.
 */
void kllineartrend_free(struct kllineartrend_context_s *ctx);

/**
 * @brief	Add a new value to the set, for the current date/time.
 * @param[in]	struct kllineartrend_context_s *ctx - Object.
 * @param[in]	double x - X axis value
 * @param[in]	double y - Y axis value
 */
void kllineartrend_add(struct kllineartrend_context_s *ctx, double x, double y);

/**
 * @brief	Print the entire lineartrend to stdout.
 * @param[in]	struct kllineartrend_statistics_s *stats - Brief description goes here.
 */
void kllineartrend_printf(struct kllineartrend_context_s *ctx);

void kllineartrend_calculate(struct kllineartrend_context_s *ctx, double *slope, double *intercept, double *deviation);

#ifdef __cplusplus
}
#endif

#endif /* KL_LINEARTREND_H */
