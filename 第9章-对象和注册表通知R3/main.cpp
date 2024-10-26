#include <stdio.h>
#include <windows.h>
#include <string>
#include <vector>

#define ADD_PID CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define DEL_PID CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define CLEAR_PID CTL_CODE(FILE_DEVICE_UNKNOWN, 0x802, METHOD_BUFFERED, FILE_ANY_ACCESS)

enum class Options
{
	Unknown,
	Add,
	Del,
	Clear,
};

int err(const char* msg)
{
	printf("%s error = %d \n", msg, GetLastError());
	system("pause");
	return 1;
}

std::vector<DWORD> ParsePids(const char* buff[], int count)
{
	std::vector<DWORD> pids;
	for (int i = 0; i < count; i++)
	{
		pids.push_back(atoi(buff[i]));
	}
	return pids;
}

int main(int argc, const char* argv[])
{
	if (argc < 2)
	{
		return err("参数错误 Usage [add|del|clear] pid1 pid2");
	}

	Options option;
	if (_stricmp(argv[1], "add") == 0)
	{
		option = Options::Add;
	}
	else if (_stricmp(argv[1], "del") == 0)
	{
		option = Options::Del;
	}
	else if (_stricmp(argv[1], "clear") == 0)
	{
		option = Options::Clear;
	}
	else
	{
		option = Options::Unknown;
	}

	auto hFile = CreateFile(L"\\\\.\\ProcessProtect", GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
	if (hFile == INVALID_HANDLE_VALUE)
	{
		return err("打开设备失败");
	}

	std::vector<DWORD> pids;
	BOOL ret = FALSE;
	DWORD bytes;

	switch (option)
	{
	case Options::Add:
	{
		pids = ParsePids(argv + 2, argc - 2);
		ret = DeviceIoControl(hFile, ADD_PID, pids.data(), pids.size() * sizeof(DWORD), nullptr, 0, &bytes, nullptr);
	}
	break;
	case Options::Del:
	{
		pids = ParsePids(argv + 2, argc - 2);
		ret = DeviceIoControl(hFile, DEL_PID, pids.data(), pids.size() * sizeof(DWORD), nullptr, 0, &bytes, nullptr);
	}
	break;
	case Options::Clear:
	{
		ret = DeviceIoControl(hFile, CLEAR_PID, nullptr, 0, nullptr, 0, &bytes, nullptr);
	}
	break;
	}

	if (!ret)
	{
		return err("DeviceIoControl error");
	}

	CloseHandle(hFile);

	system("pause");
	return 0;
}