#include <stdio.h>
#include <windows.h>
#include <string>

int err(const char* msg)
{
	printf("%s error = %d \n", msg, GetLastError());
	return 1;
}

int wmain(int argc, const wchar_t* argv[])
{
	if (argc < 2)
	{
		return err("²ÎÊý´íÎó Usage <filename>");
	}

	std::wstring stream(argv[1]);
	stream += L":backup";

	printf("%ws \n", stream.c_str());

	HANDLE hSource = CreateFile(stream.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
	if (hSource == INVALID_HANDLE_VALUE)
	{
		return err("Failed to locate backup");
	}
		
	HANDLE hTarget = CreateFile(argv[1], GENERIC_WRITE, 0, nullptr, TRUNCATE_EXISTING, 0, nullptr);
	if (hTarget == INVALID_HANDLE_VALUE)
	{
		return err("Failed to locate file");
	}

	LARGE_INTEGER size;
	if (!GetFileSizeEx(hSource, &size))
	{
		return err("Failed to get file size");
	}
		
	ULONG bufferSize = (ULONG)min((LONGLONG)1 << 21, size.QuadPart);
	void* buffer = VirtualAlloc(nullptr, bufferSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	if (!buffer)
	{
		return err("Failed to allocate buffer");
	}

	DWORD bytes;
	while (size.QuadPart > 0) 
	{
		if (!ReadFile(hSource, buffer, (DWORD)(min((LONGLONG)bufferSize, size.QuadPart)), &bytes, nullptr))
		{
			return err("Failed to read data");
		}

		if (!WriteFile(hTarget, buffer, bytes, &bytes, nullptr))
		{
			return err("Failed to write data");
		}

		size.QuadPart -= bytes;
	}

	printf("Restore successful!\n");

	CloseHandle(hSource);
	CloseHandle(hTarget);
	VirtualFree(buffer, 0, MEM_RELEASE);

	return 0;
}