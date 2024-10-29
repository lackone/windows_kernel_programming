#include <stdio.h>
#include <windows.h>
#include <fltUser.h>
#include <string>

#pragma comment(lib, "fltlib")

struct FileBackPortMsg
{
	USHORT fileNameLen;
	WCHAR fileName[1];
};

void handleMsg(byte* buf)
{
	auto msg = (FileBackPortMsg*)buf;
	std::wstring filename(msg->fileName, msg->fileNameLen);

	printf("file back up %ws \n", filename.c_str());
}

int main()
{
	HANDLE port;
	auto hr = FilterConnectCommunicationPort(L"\\FileBackPort", 0, nullptr, 0, nullptr, &port);
	if (FAILED(hr))
	{
		printf("error connect to port = %08x \n", hr);
	}

	BYTE buf[1 << 12];
	auto msg = (FILTER_MESSAGE_HEADER*)buf;

	while (true)
	{
		hr = FilterGetMessage(port, msg, sizeof(buf), nullptr);
		if (FAILED(hr))
		{
			printf("Error receiving message = %08x \n", hr);
		}

		handleMsg(buf + sizeof(FILTER_MESSAGE_HEADER));
	}

	CloseHandle(port);

	return 0;
}