#pragma once
#include <ntifs.h>

#define SET_THREAD_PRIORITY CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800 + 1, METHOD_NEITHER, FILE_ANY_ACCESS)

typedef struct _THREAD_DATA
{
	ULONG threadId; //�߳�ID
	LONG priority; //���ȼ�
} THREAD_DATA, * PTHREAD_DATA;