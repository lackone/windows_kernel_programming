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

	//��ȡ�汾��Ϣ
	RTL_OSVERSIONINFOW verion = { 0 };
	NTSTATUS status = RtlGetVersion(&verion);

	if (!NT_SUCCESS(status))
	{
		DbgPrintEx(77, 0, "RtlGetVersion = %x \n", status);
		return status;
	}

	//DWORD dwMajorVersion;       // ����ϵͳ����Ҫ�汾��
	//DWORD dwMinorVersion;       // ����ϵͳ�Ĵ�Ҫ�汾��
	//DWORD dwBuildNumber;        // ����ϵͳ���ڲ��汾��
	//DWORD dwPlatformId;         // ����ϵͳ��ƽ̨��ʶ�� (�� WINDOWS_95, WINDOWS_NT ...)
	//WCHAR szCSDVersion[128];    // �����������˵��������з�����Ļ���
	DbgPrintEx(77, 0, "��Ҫ�汾�� %u \n", verion.dwMajorVersion);
	DbgPrintEx(77, 0, "��Ҫ�汾�� %u \n", verion.dwMinorVersion);
	DbgPrintEx(77, 0, "�ڲ��汾�� %u \n", verion.dwBuildNumber);
	DbgPrintEx(77, 0, "ƽ̨��ʶ�� %u \n", verion.dwPlatformId);
	DbgPrintEx(77, 0, "�����������˵�� %ws \n", verion.szCSDVersion);

	driver->DriverUnload = DriverUnload;
	return STATUS_SUCCESS;
}