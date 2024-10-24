#include <stdio.h>
#include <windows.h>

#define SET_THREAD_PRIORITY CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800 + 1, METHOD_NEITHER, FILE_ANY_ACCESS)

typedef struct _THREAD_DATA
{
	ULONG threadId; //线程ID
	LONG priority; //优先级
} THREAD_DATA, * PTHREAD_DATA;

int main(int argc, const char* argv[])
{
	if (argc < 3)
	{
		printf("usage: <threadid> <priority> \n");
		return 0;
	}

	HANDLE hDev = CreateFile(L"\\\\.\\ThreadPriority", GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
	if (hDev == INVALID_HANDLE_VALUE)
	{
		printf("打开设备失败 \n");
		return 0;
	}

	THREAD_DATA td;
	td.threadId = atoi(argv[1]);
	td.priority = atoi(argv[2]);
	DWORD ret = 0;

	//发送控制码
	BOOL status = DeviceIoControl(hDev, SET_THREAD_PRIORITY, &td, sizeof(THREAD_DATA), NULL, 0, &ret, NULL);
	if (status)
	{
		printf("设置成功 \n");
	}
	else
	{
		printf("设置失败 \n");
	}

	CloseHandle(hDev);

	system("pause");
	return 0;
}