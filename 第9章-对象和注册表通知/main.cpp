#include <ntifs.h>
#include <ntddk.h>
#include <intrin.h>
#include "Common.h"

#define UP(P) UNREFERENCED_PARAMETER(P)

#define MAX_PIDS 256

#define ADD_PID CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define DEL_PID CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define CLEAR_PID CTL_CODE(FILE_DEVICE_UNKNOWN, 0x802, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define PROCESS_TERMINATE 1

struct Globals
{
	ULONG pidCount;
	ULONG pids[MAX_PIDS];
	FastMutex mutex;
	PVOID regHandle;
	VOID Init()
	{
		mutex.Init();
	}
};

Globals g_data;

VOID DriverUnload(PDRIVER_OBJECT DriverObject)
{
	UP(DriverObject);

	ObUnRegisterCallbacks(g_data.regHandle);

	UNICODE_STRING linkName = { 0 };
	RtlInitUnicodeString(&linkName, L"\\??\\ProcessProtect");
	IoDeleteSymbolicLink(&linkName);

	if (DriverObject->DeviceObject)
	{
		IoDeleteDevice(DriverObject->DeviceObject);
	}

	DbgPrintEx(77, 0, "DriverUnload \r\n");
}

BOOLEAN AddPid(ULONG pid)
{
	for (ULONG i = 0; i < MAX_PIDS; i++)
	{
		if (g_data.pids[i] == 0)
		{
			g_data.pids[i] = pid;
			g_data.pidCount++;
			return TRUE;
		}
	}
	return FALSE;
}

BOOLEAN delPid(ULONG pid)
{
	for (ULONG i = 0; i < MAX_PIDS; i++)
	{
		if (g_data.pids[i] == pid)
		{
			g_data.pids[i] = 0;
			g_data.pidCount--;
			return TRUE;
		}
	}
	return FALSE;
}

BOOLEAN findPid(ULONG pid)
{
	for (ULONG i = 0; i < MAX_PIDS; i++)
	{
		if (g_data.pids[i] == pid)
		{
			return TRUE;
		}
	}
	return FALSE;
}

OB_PREOP_CALLBACK_STATUS OnPreOpenProcess(
	_In_ PVOID RegistrationContext,
	_Inout_ POB_PRE_OPERATION_INFORMATION OperationInformation
	)
{
	UP(RegistrationContext);
	if (OperationInformation->KernelHandle)
	{
		//如果是内核对象，直接返回
		return OB_PREOP_SUCCESS;
	}

	auto process = (PEPROCESS)OperationInformation->Object;
	auto pid = HandleToULong(PsGetProcessId(process));

	AutoLock<FastMutex> lock(g_data.mutex);

	if (findPid(pid))
	{
		DbgPrintEx(77, 0, "找到 pid = %d \n", pid);

		DbgPrintEx(77, 0, "CreateHandleInformation OriginalDesiredAccess = %x \n", OperationInformation->Parameters->CreateHandleInformation.OriginalDesiredAccess);
		DbgPrintEx(77, 0, "DuplicateHandleInformation OriginalDesiredAccess = %x \n", OperationInformation->Parameters->DuplicateHandleInformation.OriginalDesiredAccess);

		// 移除PROCESS_TERMINATE访问掩码
		//OperationInformation->Parameters->CreateHandleInformation.DesiredAccess &= ~PROCESS_TERMINATE;
		//OperationInformation->Parameters->DuplicateHandleInformation.DesiredAccess &= ~PROCESS_TERMINATE;
		//OperationInformation->Parameters->CreateHandleInformation.OriginalDesiredAccess &= ~PROCESS_TERMINATE;
		//OperationInformation->Parameters->DuplicateHandleInformation.OriginalDesiredAccess &= ~PROCESS_TERMINATE;

		OperationInformation->Parameters->CreateHandleInformation.DesiredAccess = 0;
		OperationInformation->Parameters->DuplicateHandleInformation.DesiredAccess = 0;
		OperationInformation->Parameters->CreateHandleInformation.OriginalDesiredAccess = 0;
		OperationInformation->Parameters->DuplicateHandleInformation.OriginalDesiredAccess = 0;

		DbgPrintEx(77, 0, "CreateHandleInformation DesiredAccess = %x \n", OperationInformation->Parameters->CreateHandleInformation.DesiredAccess);
		DbgPrintEx(77, 0, "DuplicateHandleInformation DesiredAccess = %x \n", OperationInformation->Parameters->DuplicateHandleInformation.DesiredAccess);
	}

	return OB_PREOP_SUCCESS;
}

NTSTATUS createClose(
	_In_ struct _DEVICE_OBJECT* DeviceObject,
	_Inout_ struct _IRP* Irp
)
{
	UP(DeviceObject);
	Irp->IoStatus.Status = STATUS_SUCCESS;
	Irp->IoStatus.Information = 0;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	return STATUS_SUCCESS;
}

NTSTATUS ctl(
	_In_ struct _DEVICE_OBJECT* DeviceObject,
	_Inout_ struct _IRP* Irp
)
{
	UP(DeviceObject);
	auto stack = IoGetCurrentIrpStackLocation(Irp);
	auto status = STATUS_SUCCESS;
	auto len = 0;

	switch (stack->Parameters.DeviceIoControl.IoControlCode)
	{
	case ADD_PID:
	{
		auto size = stack->Parameters.DeviceIoControl.InputBufferLength;
		if (size % sizeof(ULONG) != 0)
		{
			status = STATUS_INVALID_BUFFER_SIZE;
			break;
		}

		auto data = (ULONG*)Irp->AssociatedIrp.SystemBuffer;

		AutoLock<FastMutex> lock(g_data.mutex);

		for (ULONG i = 0; i < size / sizeof(ULONG); i++)
		{
			auto pid = data[i];
			if (pid == 0)
			{
				status = STATUS_INVALID_PARAMETER;
				break;
			}
			if (findPid(pid))
			{
				continue;
			}
			if (g_data.pidCount == MAX_PIDS)
			{
				status = STATUS_TOO_MANY_CONTEXT_IDS;
				break;
			}
			if (!AddPid(pid))
			{
				status = STATUS_UNSUCCESSFUL;
				break;
			}
			DbgPrintEx(77, 0, "添加 pid = %d \n", pid);
			len += sizeof(ULONG);
		}

		break;
	}
	case DEL_PID:
	{
		auto size = stack->Parameters.DeviceIoControl.InputBufferLength;
		if (size % sizeof(ULONG) != 0)
		{
			status = STATUS_INVALID_BUFFER_SIZE;
			break;
		}

		auto data = (ULONG*)Irp->AssociatedIrp.SystemBuffer;

		AutoLock<FastMutex> lock(g_data.mutex);

		for (ULONG i = 0; i < size / sizeof(ULONG); i++)
		{
			auto pid = data[i];
			if (pid == 0)
			{
				status = STATUS_INVALID_PARAMETER;
				break;
			}
			if (!delPid(pid))
			{
				continue;
			}
			DbgPrintEx(77, 0, "删除 pid = %d \n", pid);
			len += sizeof(ULONG);
			if (g_data.pidCount == 0)
			{
				break;
			}
		}

		break;
	}
	case CLEAR_PID:
	{
		AutoLock<FastMutex> lock(g_data.mutex);

		memset(&g_data.pids, 0, sizeof(g_data.pids));

		g_data.pidCount = 0;

		break;
	}
	default:
		status = STATUS_INVALID_DEVICE_REQUEST;
		break;
	}

	Irp->IoStatus.Status = status;
	Irp->IoStatus.Information = len;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	return status;
}

extern "C" NTSTATUS DriverEntry(PDRIVER_OBJECT driver, PUNICODE_STRING reg)
{
	UP(reg);

	auto status = STATUS_SUCCESS;

	g_data.Init();

	OB_OPERATION_REGISTRATION opers[] = {
		{
			PsProcessType,
			OB_OPERATION_HANDLE_CREATE | OB_OPERATION_HANDLE_DUPLICATE,
			OnPreOpenProcess,
			nullptr
		},
	};

	OB_CALLBACK_REGISTRATION regs = {
		OB_FLT_REGISTRATION_VERSION,
		1,
		RTL_CONSTANT_STRING(L"12345.678"),
		nullptr,
		opers
	};

	PDEVICE_OBJECT devObj = NULL;
	UNICODE_STRING devName = { 0 };
	UNICODE_STRING linkName = { 0 };
	BOOLEAN linkCreate = FALSE;

	RtlInitUnicodeString(&devName, L"\\Device\\ProcessProtect");
	RtlInitUnicodeString(&linkName, L"\\??\\ProcessProtect");

	do 
	{
		status = IoCreateDevice(driver, 0, &devName, FILE_DEVICE_UNKNOWN, 0, FALSE, &devObj);
		if (!NT_SUCCESS(status))
		{
			DbgPrintEx(77, 0, "IoCreateDevice = %x \n", status);
			break;
		}
		devObj->Flags |= DO_BUFFERED_IO;

		status = IoCreateSymbolicLink(&linkName, &devName);
		if (!NT_SUCCESS(status))
		{
			DbgPrintEx(77, 0, "IoCreateSymbolicLink = %x \n", status);
			break;
		}
		linkCreate = TRUE;

		status = ObRegisterCallbacks(&regs, &g_data.regHandle);
		if (!NT_SUCCESS(status))
		{
			break;
		}

	} while (0);

	if (!NT_SUCCESS(status))
	{
		if (linkCreate)
		{
			IoDeleteSymbolicLink(&linkName);
		}
		if (devObj)
		{
			IoDeleteDevice(devObj);
		}
	}

	driver->MajorFunction[IRP_MJ_CREATE] = createClose;
	driver->MajorFunction[IRP_MJ_CLOSE] = createClose;
	driver->MajorFunction[IRP_MJ_DEVICE_CONTROL] = ctl;

	driver->DriverUnload = DriverUnload;
	return STATUS_SUCCESS;
}