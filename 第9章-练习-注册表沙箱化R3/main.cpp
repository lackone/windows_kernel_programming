#include <stdio.h>
#include <windows.h>

#define ADD_REG CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define DEL_REG CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define CLEAR_REG CTL_CODE(FILE_DEVICE_UNKNOWN, 0x802, METHOD_NEITHER, FILE_ANY_ACCESS)

typedef struct _RegItem
{
	WCHAR originalKey[256];
	WCHAR sandboxKey[256];
} RegItem, * PRegItem;

int err(const char* msg)
{
	printf("%s error = %d \n", msg, GetLastError());
	return 1;
}

int wmain(int argc, const wchar_t* argv[])
{
	auto hFile = CreateFile(L"\\\\.\\RegSandbox", GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
	if (hFile == INVALID_HANDLE_VALUE)
	{
		return err("打开设备失败");
	}

	BOOL ret = FALSE;
	DWORD bytes;

	RegItem item = { 0 };

	//把对 aaa 的写入，全部重定向到 aaa_sandbox
	wcscpy_s(item.originalKey, 256, L"\\REGISTRY\\USER\\S-1-5-21-1640836768-2440225482-3352614534-1001\\aaa");
	wcscpy_s(item.sandboxKey, 256, L"\\REGISTRY\\USER\\S-1-5-21-1640836768-2440225482-3352614534-1001\\aaa_sandbox");

	//wcscpy_s(item.originalKey, 256, L"aaa");
	//wcscpy_s(item.sandboxKey, 256, L"aaa_sandbox");

	printf("%ws %ws \n", item.originalKey, item.sandboxKey);

	ret = DeviceIoControl(hFile, ADD_REG, (LPVOID)&item, sizeof(item), nullptr, 0, &bytes, nullptr);

	system("pause");

	HKEY hKey;
	LONG res = RegCreateKeyEx(HKEY_CURRENT_USER, L"aaa", 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &hKey, nullptr);
	if (res == ERROR_SUCCESS)
	{
		CHAR* str = (CHAR*)"12345";

		res = RegSetValueExA(hKey, "mmmmmm", 0, REG_SZ, (BYTE*)str, strlen(str) + 1);

		if (res == ERROR_SUCCESS)
		{
			printf("设置成功\n");
		}

		BYTE buf[256] = { 0 };
		DWORD type = 0;
		DWORD size = 0;
		RegQueryValueExA(hKey, "mmmmmm", nullptr, &type, buf, &size);

		printf("获取数据 %s \n", buf);
	}

	system("pause");

	CloseHandle(hFile);

	system("pause");
	return 0;
}