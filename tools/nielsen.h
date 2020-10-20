/* Copyright (c) 2020 LTN Global Inc. All Rights Reserved. */

#ifndef KLVANC_NIELSEN_H
#define KLVANC_NIELSEN_H

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
/* Nielsen SDK for audio monitoring */
#include <IMonitorSdkCallback.h>
#include <IMonitorSdkProcessor.h>
#include <MonitorSdkParameters.h>
#include <MonitorSdkSharedDefines.h>
#include <MonitorApi.h>

class CMonitorSdkCallback : public IMonitorSdkCallback
{
public:
	CMonitorSdkCallback(int pairNumber);
	virtual ~CMonitorSdkCallback();

	virtual void ResultCallback(uint32_t elapsed_time, std::string result) override;
	virtual void LogCallback(int code, const char* pMessage) override;
	virtual void AlarmCallback(uint32_t elapsed_time, std::string warning_list) override;

private:
	int pairNumber;
};

#endif /* ENABLE_NIELSEN */

#endif /* KLVANC_NIELSEN_H */
