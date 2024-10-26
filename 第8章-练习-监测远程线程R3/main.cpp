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
	ULONG pid;
} Package, * PPackage;

int err(const char* msg)
{
	printf("%s error = %d \n", msg, GetLastError());
	system("pause");
	return 1;
}

int main()
{
	auto hFile = CreateFile(L"\\\\.\\RemoteThreadMon", GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
	if (hFile == INVALID_HANDLE_VALUE)
	{
		return err("���豸ʧ��");
	}

	ULONG pid = 1000;

	PPackage package = (PPackage)malloc(sizeof(Package));

	if (!package)
	{
		return err("malloc error");
	}

	package->type = Type::TYPE_ADD;
	package->pid = pid;

	DWORD bytes;
	if (!WriteFile(hFile, package, sizeof(Package), &bytes, nullptr))
	{
		return err("д��ʧ��");
	}

	system("pause");

	package->type = Type::TYPE_DEL;

	if (!WriteFile(hFile, package, sizeof(Package), &bytes, nullptr))
	{
		return err("д��ʧ��");
	}

	free(package);

	CloseHandle(hFile);

	system("pause");
	return 0;
}