#include <stdio.h>
#include <windows.h>

#define ADD_DEVICE CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define REMOVE_DEVICE CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define REMOVE_ALL CTL_CODE(FILE_DEVICE_UNKNOWN, 0x802, METHOD_NEITHER, FILE_ANY_ACCESS)

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
	return 1;
}

int wmain(int argc, const wchar_t* argv[])
{
	if (argc < 2)
	{
		return err("参数错误 Usage [add|del|clear] \\Device\\xxx");
	}

	Options option;
	if (_wcsicmp(argv[1], L"add") == 0)
	{
		option = Options::Add;
	}
	else if (_wcsicmp(argv[1], L"del") == 0)
	{
		option = Options::Del;
	}
	else if (_wcsicmp(argv[1], L"clear") == 0)
	{
		option = Options::Clear;
	}
	else
	{
		option = Options::Unknown;
	}

	auto hFile = CreateFile(L"\\\\.\\DevMonManager", GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
	if (hFile == INVALID_HANDLE_VALUE)
	{
		return err("打开设备失败");
	}

	BOOL ret = FALSE;
	DWORD bytes;

	printf("option = %d \n", option);

	switch (option)
	{
	case Options::Add:
	{
		printf("%ws \n", argv[2]);
		ret = DeviceIoControl(hFile, ADD_DEVICE, (LPVOID)argv[2], (wcslen(argv[2]) + 1) * sizeof(wchar_t), nullptr, 0, &bytes, nullptr);
	}
	break;
	case Options::Del:
	{
		printf("%ws \n", argv[2]);
		ret = DeviceIoControl(hFile, REMOVE_DEVICE, (LPVOID)argv[2], (wcslen(argv[2]) + 1) * sizeof(wchar_t), nullptr, 0, &bytes, nullptr);
	}
	break;
	case Options::Clear:
	{
		ret = DeviceIoControl(hFile, REMOVE_ALL, nullptr, 0, nullptr, 0, &bytes, nullptr);
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