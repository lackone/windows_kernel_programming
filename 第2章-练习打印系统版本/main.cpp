#include <ntifs.h>
#include <ntddk.h>
#include <intrin.h>

#define UP(P) UNREFERENCED_PARAMETER(P)

VOID DriverUnload(PDRIVER_OBJECT DriverObject)
{
	UP(DriverObject);

	DbgPrintEx(77, 0, "DriverUnload \r\n");
}

extern "C" NTSTATUS DriverEntry(PDRIVER_OBJECT driver, PUNICODE_STRING reg)
{
	UP(reg);

	//获取版本信息
	RTL_OSVERSIONINFOW verion = { 0 };
	NTSTATUS status = RtlGetVersion(&verion);

	if (!NT_SUCCESS(status))
	{
		DbgPrintEx(77, 0, "RtlGetVersion = %x \n", status);
		return status;
	}

	//DWORD dwMajorVersion;       // 操作系统的主要版本号
	//DWORD dwMinorVersion;       // 操作系统的次要版本号
	//DWORD dwBuildNumber;        // 操作系统的内部版本号
	//DWORD dwPlatformId;         // 操作系统的平台标识符 (如 WINDOWS_95, WINDOWS_NT ...)
	//WCHAR szCSDVersion[128];    // 包含服务包的说明（如果有服务包的话）
	DbgPrintEx(77, 0, "主要版本号 %u \n", verion.dwMajorVersion);
	DbgPrintEx(77, 0, "次要版本号 %u \n", verion.dwMinorVersion);
	DbgPrintEx(77, 0, "内部版本号 %u \n", verion.dwBuildNumber);
	DbgPrintEx(77, 0, "平台标识符 %u \n", verion.dwPlatformId);
	DbgPrintEx(77, 0, "包含服务包的说明 %ws \n", verion.szCSDVersion);

	driver->DriverUnload = DriverUnload;
	return STATUS_SUCCESS;
}