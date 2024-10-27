#include <stdio.h>
#include <windows.h>
#include <string>

enum class ItemType
{
	None,
	RegSetValue
};

//ͨ����Ϣͷ
struct ItemHeader
{
	ItemType type; //����
	USHORT size; //�ṹ��С
	LARGE_INTEGER time; //ϵͳʱ��
};

//ע���������Ϣ
struct RegSetValueInfo : ItemHeader
{
	ULONG pid;
	ULONG tid;
	WCHAR keyName[256];
	WCHAR valueName[64];
	ULONG dataType;
	UCHAR data[128];
	ULONG dataSize;
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

void displayBinary(const UCHAR* buf, DWORD size)
{
	for (DWORD i = 0; i < size; i++)
	{
		printf("%02X ", buf[i]);
	}
	printf("\n");
}

void displayInfo(BYTE* buf, DWORD size)
{
	auto count = size;

	while (count > 0)
	{
		auto header = (ItemHeader*)buf;

		switch (header->type)
		{
		case ItemType::RegSetValue:
		{
			displayTime(header->time);

			auto info = (RegSetValueInfo*)buf;

			printf("reg write pid=%d %ws\\%ws type:%d size:%d data: ", info->pid, info->keyName, info->valueName, info->dataType, info->dataSize);

			switch (info->dataType)
			{
			case REG_DWORD:
				printf("0x%08X\n", *(DWORD*)info->data);
				break;
			case REG_SZ:
			case REG_EXPAND_SZ:
				printf("%ws\n", (WCHAR*)info->data);
				break;
			case REG_BINARY:
				displayBinary(info->data, min(info->dataSize, sizeof(info->data)));
				break;
			default:
				displayBinary(info->data, min(info->dataSize, sizeof(info->data)));
				break;
			}

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
		return err("���豸ʧ��");
	}

	BYTE buf[1 << 12];

	while (true)
	{
		DWORD bytes;
		if (!ReadFile(hFile, buf, sizeof(buf), &bytes, nullptr))
		{
			return err("��ȡʧ��");
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