#include <stdio.h>
#include <windows.h>

int err(const char* msg)
{
	printf("%s error = %d \n", msg, GetLastError());
	return 1;
}

int main()
{
	HANDLE hDev = CreateFile(L"\\\\.\\Zero", GENERIC_WRITE | GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);

	if (hDev == INVALID_HANDLE_VALUE)
	{
		return err("打开设备失败");
	}

	BYTE buf[0x20] = { 0 };

	for (int i = 0; i < 0x20; i++)
	{
		buf[i] = i + 1;
	}

	DWORD ret = 0;
	BOOL ok =  ReadFile(hDev, buf, sizeof(buf), &ret, nullptr);
	if (!ok)
	{
		return err("读数据失败");
	}
	printf("ret = %d \n", ret);

	for (int i = 0; i < 0x20; i++)
	{
		printf("%d ", buf[i]);
	}

	BYTE buf2[0x100] = { 0 };

	ok = WriteFile(hDev, buf2, sizeof(buf2), &ret, nullptr);
	if (!ok)
	{
		return err("写数据失败");
	}
	printf("ret = %d \n", ret);

	CloseHandle(hDev);

	system("pause");
	return 0;
}