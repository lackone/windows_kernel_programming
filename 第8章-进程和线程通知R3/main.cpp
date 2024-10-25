#include <stdio.h>
#include <windows.h>
#include <string>

enum class ItemType
{
	None,
	ProcessCreate,
	ProcessExit,
	ThreadCreate,
	ThreadExit,
	ImageLoad
};

//通用信息头
struct ItemHeader
{
	ItemType type; //类型
	USHORT size; //结构大小
	LARGE_INTEGER time; //系统时间
};

//进程退出
struct ProcessExitInfo : ItemHeader
{
	ULONG pid;
};

//进程创建
struct ProcessCreateInfo : ItemHeader
{
	ULONG pid;
	ULONG ppid;
	USHORT cmdLen;
	USHORT cmdOffset;
	USHORT imgLen;
	USHORT imgOffset;
};

//线程创建退出信息
struct ThreadCreateExitInfo : ItemHeader
{
	ULONG tid;
	ULONG pid;
};

//模块加载信息
struct ImageLoadInfo : ItemHeader
{
	ULONG pid;
	ULONG_PTR imageBase;
	ULONG_PTR imageSize;
	USHORT pathLen;
	USHORT pathOffset;
};

int err(const char* msg)
{
	printf("%s error = %d \n", msg, GetLastError());
	return 1;
}

void displayTime(const LARGE_INTEGER& time)
{
	SYSTEMTIME st;
	::FileTimeToSystemTime((FILETIME*)&time, &st);
	printf("%02d:%02d:%02d.%03d ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
}

void displayInfo(BYTE* buf, DWORD size)
{
	auto count = size;

	while (count > 0)
	{
		auto header = (ItemHeader*)buf;

		switch (header->type)
		{
		case ItemType::ProcessCreate:
		{
			displayTime(header->time);
			auto info = (ProcessCreateInfo*)buf;
			std::wstring cmd((WCHAR*)(buf + info->cmdOffset), info->cmdLen);
			std::wstring img((WCHAR*)(buf + info->imgOffset), info->imgLen);

			printf("process %d cmd [%ws] img [%ws] \n", info->pid, cmd.c_str(), img.c_str());
			break;
		}
		case ItemType::ProcessExit:
		{
			displayTime(header->time);
			auto info = (ProcessExitInfo*)buf;
			printf("process %d exit\n", info->pid);
			break;
		}
		case ItemType::ThreadCreate:
		{
			displayTime(header->time);
			auto info = (ThreadCreateExitInfo*)buf;
			printf("thread %d create in process %d \n", info->tid, info->pid);
			break;
		}
		case ItemType::ThreadExit:
		{
			displayTime(header->time);
			auto info = (ThreadCreateExitInfo*)buf;
			printf("thread %d exit from process %d \n", info->tid, info->pid);
			break;
		}
		case ItemType::ImageLoad:
		{
			displayTime(header->time);
			auto info = (ImageLoadInfo*)buf;
			std::wstring path((WCHAR*)(buf + info->pathOffset), info->pathLen);

			printf("image load %d %ws base=%llx size=%llx \n", info->pid, path.c_str(), info->imageBase, info->imageSize);
			break;
		}
		}

		buf += header->size;
		count -= header->size;
	}
}

int main()
{
	auto hFile = CreateFile(L"\\\\.\\sysmon", GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
	if (hFile == INVALID_HANDLE_VALUE)
	{
		return err("打开设备失败");
	}

	BYTE buf[1 << 12];

	while (true)
	{
		DWORD bytes;
		if (!ReadFile(hFile, buf, sizeof(buf), &bytes, nullptr))
		{
			return err("读取失败");
		}

		if (bytes != 0)
		{
			displayInfo(buf, bytes);
		}

		Sleep(200);
	}

	CloseHandle(hFile);

	system("pause");
	return 0;
}