/* Copyright (c) 2020 LTN Global Inc. All Rights Reserved. */

#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>
#include <assert.h>
#include <libgen.h>
#include <signal.h>
#include <limits.h>

#if HAVE_IMONITORSDKPROCESSOR_H
#define ENABLE_NIELSEN 1
#endif

#if ENABLE_NIELSEN

#include "nielsen.h"

/* Nielsen SDK for audio monitoring */
#include <IMonitorSdkCallback.h>
#include <IMonitorSdkProcessor.h>
#include <MonitorSdkParameters.h>
#include <MonitorSdkSharedDefines.h>
#include <MonitorApi.h>

CMonitorSdkCallback::CMonitorSdkCallback(int pairNumber)
{
	this->pairNumber = pairNumber;
}

CMonitorSdkCallback::~CMonitorSdkCallback()
{
}

void CMonitorSdkCallback::ResultCallback(uint32_t elapsed_time, std::string result)
{
	printf("Nielsen pair %02d: %d, %s\n", pairNumber, elapsed_time, result.c_str());
};

void CMonitorSdkCallback::LogCallback(int code, const char* pMessage)
{
	printf("Nielsen pair %02d: %d, %s\n", pairNumber, code, pMessage);
};

void CMonitorSdkCallback::AlarmCallback(uint32_t elapsed_time, std::string warning_list)
{
	printf("Nielsen pair %02d: %d, %s\n", pairNumber, elapsed_time, warning_list.c_str());
};

#endif /* ENABLE_NIELSEN */
