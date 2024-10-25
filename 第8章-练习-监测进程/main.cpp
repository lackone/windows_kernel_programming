#include <ntifs.h>
#include <ntddk.h>
#include <intrin.h>
#include "Common.h"

#define UP(P) UNREFERENCED_PARAMETER(P)
#define DRIVER_TAG 'PM'

ProcessMonList g_ProcessMon;

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

VOID addNameToList(PWCH str, SIZE_T len)
{
	AutoLock<FastMutex> lock(g_ProcessMon.mutex);

	PProcessMonItem item = (PProcessMonItem)ExAllocatePoolWithTag(PagedPool, sizeof(ProcessMonItem), DRIVER_TAG);
	if (item == nullptr)
	{
		return;
	}

	PWCH mem = (PWCH)ExAllocatePoolWithTag(PagedPool, len, DRIVER_TAG);
	if (mem == nullptr)
	{
		return;
	}

	memset(mem, 0, len);
	memcpy(mem, str, len);

	_wcsupr(mem);
	
	item->name = mem;

	InsertTailList(&g_ProcessMon.head, &item->entry);

	DbgPrintEx(77, 0, "添加 %ws \n", mem);
}

VOID delNameFromList(PWCH str)
{
	AutoLock<FastMutex> lock(g_ProcessMon.mutex);

	PLIST_ENTRY next = g_ProcessMon.head.Flink;
	PLIST_ENTRY find = NULL;

	_wcsupr(str);

	while (next != &g_ProcessMon.head)
	{
		PProcessMonItem item = CONTAINING_RECORD(next, ProcessMonItem, entry);

		if (wcsstr(item->name, str))
		{
			DbgPrintEx(77, 0, "找到 %ws \n", item->name);

			find = next;

			break;
		}

		next = next->Flink;
	}

	if (find)
	{
		RemoveEntryList(find);
	}
}

VOID killProcess(
	_In_ PVOID StartContext
)
{
	HANDLE pid = (HANDLE)StartContext;
	HANDLE handle = NULL;
	OBJECT_ATTRIBUTES obj = { 0 };
	CLIENT_ID clientId = { 0 };
	clientId.UniqueProcess = pid;

	DbgPrintEx(77, 0, "pid = %d \n", pid);

	InitializeObjectAttributes(&obj, NULL, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);

	LARGE_INTEGER time = { 0 };
	time.QuadPart = -10000 * 200;
	ULONG cnt = 0;

	while (TRUE)
	{
		if (cnt > 999)
		{
			break;
		}

		auto status = ZwOpenProcess(&handle, GENERIC_ALL, &obj, &clientId);
		if (NT_SUCCESS(status))
		{
			break;
		}

		DbgPrintEx(77, 0, "ZwOpenProcess = %x \n", status);

		KeDelayExecutionThread(KernelMode, FALSE, &time);

		cnt++;
	}

	if (handle)
	{
		ZwTerminateProcess(handle, 0);
		ZwClose(handle);
	}
}

VOID ProcessNotify(
	_Inout_ PEPROCESS Process,
	_In_ HANDLE ProcessId,
	_Inout_opt_ PPS_CREATE_NOTIFY_INFO CreateInfo
	)
{
	UP(Process);
	if (CreateInfo)
	{
		PWCH fileName = (PWCH)ExAllocatePoolWithTag(PagedPool, CreateInfo->ImageFileName->Length + 2, DRIVER_TAG);

		if (fileName == nullptr)
		{
			return;
		}

		memset(fileName, 0, CreateInfo->ImageFileName->Length + 2);
		memcpy(fileName, CreateInfo->ImageFileName->Buffer, CreateInfo->ImageFileName->Length);
		_wcsupr(fileName);

		PLIST_ENTRY next = g_ProcessMon.head.Flink;

		while (next != &g_ProcessMon.head)
		{
			PProcessMonItem item = CONTAINING_RECORD(next, ProcessMonItem, entry);

			if (wcsstr(fileName, item->name))
			{
				DbgPrintEx(77, 0, "找到 %ws \n", fileName);

				//直接杀死进程
				HANDLE handle = NULL;

				PsCreateSystemThread(&handle, 0, NULL, NULL, NULL, killProcess, (PVOID)ProcessId);

				ZwClose(handle);

				break;
			}

			next = next->Flink;
		}
	}
}

NTSTATUS write(
	_In_ struct _DEVICE_OBJECT* DeviceObject,
	_Inout_ struct _IRP* Irp
)
{
	DbgPrintEx(77, 0, "write \n");

	UP(DeviceObject);
	auto status = STATUS_SUCCESS;
	auto count = 0;

	auto buf = (PUCHAR)MmGetSystemAddressForMdlSafe(Irp->MdlAddress, NormalPagePriority);
	if (!buf)
	{
		status = STATUS_INSUFFICIENT_RESOURCES;
	}
	else
	{
		PPackage package = (PPackage)buf;

		switch (package->type)
		{
		case Type::TYPE_ADD:
		{
			DbgPrintEx(77, 0, "%ws \n", (PWCH)(buf + package->nameOffset));

			addNameToList((PWCH)(buf + package->nameOffset), package->nameLen);

			break;
		}
		case Type::TYPE_DEL:
		{
			DbgPrintEx(77, 0, "%ws \n", (PWCH)(buf + package->nameOffset));

			delNameFromList((PWCH)(buf + package->nameOffset));

			break;
		}
		default:
		{
			status = STATUS_INVALID_PARAMETER;
			break;
		}
		}
	}

	DbgPrintEx(77, 0, "status = %x \n", status);

	Irp->IoStatus.Status = status;
	Irp->IoStatus.Information = count;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	return status;
}

VOID DriverUnload(PDRIVER_OBJECT DriverObject)
{
	UP(DriverObject);

	PsSetCreateProcessNotifyRoutineEx(ProcessNotify, TRUE);

	UNICODE_STRING linkName = { 0 };
	RtlInitUnicodeString(&linkName, L"\\??\\ProcessMon");
	IoDeleteSymbolicLink(&linkName);

	if (DriverObject->DeviceObject)
	{
		IoDeleteDevice(DriverObject->DeviceObject);
	}

	while (!IsListEmpty(&g_ProcessMon.head))
	{
		auto entry = RemoveHeadList(&g_ProcessMon.head);

		PProcessMonItem item = CONTAINING_RECORD(entry, ProcessMonItem, entry);

		if (item->name)
		{
			ExFreePoolWithTag(item->name, DRIVER_TAG);
		}

		ExFreePoolWithTag(item, DRIVER_TAG);
	}

	DbgPrintEx(77, 0, "DriverUnload \r\n");
}

extern "C" NTSTATUS DriverEntry(PDRIVER_OBJECT driver, PUNICODE_STRING reg)
{
	UP(reg);

	auto status = STATUS_SUCCESS;

	g_ProcessMon.mutex.Init();

	InitializeListHead(&g_ProcessMon.head);

	PDEVICE_OBJECT devObj = NULL;
	UNICODE_STRING devName = { 0 };
	UNICODE_STRING linkName = { 0 };

	RtlInitUnicodeString(&devName, L"\\Device\\ProcessMon");
	RtlInitUnicodeString(&linkName, L"\\??\\ProcessMon");

	do
	{
		status = IoCreateDevice(driver, 0, &devName, FILE_DEVICE_UNKNOWN, 0, FALSE, &devObj);
		if (!NT_SUCCESS(status))
		{
			DbgPrintEx(77, 0, "IoCreateDevice = %x \n", status);
			break;
		}
		devObj->Flags |= DO_DIRECT_IO;

		status = IoCreateSymbolicLink(&linkName, &devName);
		if (!NT_SUCCESS(status))
		{
			DbgPrintEx(77, 0, "IoCreateSymbolicLink = %x \n", status);
			break;
		}

		status = PsSetCreateProcessNotifyRoutineEx(ProcessNotify, FALSE);
		if (!NT_SUCCESS(status))
		{
			DbgPrintEx(77, 0, "PsSetCreateProcessNotifyRoutineEx = %x \n", status);
			break;
		}

	} while (0);

	DbgPrintEx(77, 0, "status = %x \n", status);

	driver->MajorFunction[IRP_MJ_CREATE] = createClose;
	driver->MajorFunction[IRP_MJ_CLOSE] = createClose;
	driver->MajorFunction[IRP_MJ_WRITE] = write;

	driver->DriverUnload = DriverUnload;
	return STATUS_SUCCESS;
}