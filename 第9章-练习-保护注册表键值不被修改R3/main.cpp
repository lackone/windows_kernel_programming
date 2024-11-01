#include <stdio.h>
#include <windows.h>

#define ADD_REG CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define DEL_REG CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define CLEAR_REG CTL_CODE(FILE_DEVICE_UNKNOWN, 0x802, METHOD_NEITHER, FILE_ANY_ACCESS)

typedef struct _RegItem
{
	WCHAR keyName[256];
	WCHAR valueName[256];
} RegItem, * PRegItem;

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
		return err("参数错误 Usage [add|del|clear] xxx xxx");
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

	auto hFile = CreateFile(L"\\\\.\\RegProtect", GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
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
		RegItem item = { 0 };
		wcscpy_s(item.keyName, 256, argv[2]);
		wcscpy_s(item.valueName, 256, argv[3]);

		printf("%ws %ws \n", item.keyName, item.valueName);

		ret = DeviceIoControl(hFile, ADD_REG, (LPVOID)&item, sizeof(item), nullptr, 0, &bytes, nullptr);
	}
	break;
	case Options::Del:
	{
		RegItem item = { 0 };
		wcscpy_s(item.keyName, 256, argv[2]);
		wcscpy_s(item.valueName, 256, argv[3]);

		printf("%ws %ws \n", item.keyName, item.valueName);

		ret = DeviceIoControl(hFile, DEL_REG, (LPVOID)&item, sizeof(item), nullptr, 0, &bytes, nullptr);
	}
	break;
	case Options::Clear:
	{
		ret = DeviceIoControl(hFile, CLEAR_REG, nullptr, 0, nullptr, 0, &bytes, nullptr);
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