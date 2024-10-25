#include <stdio.h>
#include <windows.h>
#include <string>

enum class Type
{
	TYPE_ADD,
	TYPE_DEL
};

typedef struct _Package
{
	Type type;
	USHORT nameLen;
	USHORT nameOffset;
} Package, * PPackage;

int err(const char* msg)
{
	printf("%s error = %d \n", msg, GetLastError());
	system("pause");
	return 1;
}

int main()
{
	auto hFile = CreateFile(L"\\\\.\\ProcessMon", GENERIC_READ|GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
	if (hFile == INVALID_HANDLE_VALUE)
	{
		return err("打开设备失败");
	}

	LPCWCH name = L"notepad.exe";
	int len = wcslen(name) * sizeof(WCHAR) + 2;
	int size = sizeof(Package) + len;

	PPackage package = (PPackage)malloc(size);

	if (!package)
	{
		return err("malloc error");
	}

	memset(package, 0, size);
	package->type = Type::TYPE_ADD;
	package->nameLen = len;
	package->nameOffset = sizeof(Package);

	printf("len = %d offset = %d \n", len, sizeof(Package));

	memcpy((PUCHAR)package + package->nameOffset, name, len);

	DWORD bytes;
	if (!WriteFile(hFile, package, size, &bytes, nullptr))
	{
		return err("写入失败");
	}

	system("pause");

	package->type = Type::TYPE_DEL;

	if (!WriteFile(hFile, package, size, &bytes, nullptr))
	{
		return err("写入失败");
	}

	free(package);

	CloseHandle(hFile);

	system("pause");
	return 0;
}