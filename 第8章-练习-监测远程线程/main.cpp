#include <ntifs.h>
#include <ntddk.h>
#include <intrin.h>
#include "Common.h"

#define UP(P) UNREFERENCED_PARAMETER(P)
#define DRIVER_TAG 'TM'

ThreadMonList g_ThreadMonList;

extern "C" NTSTATUS NTAPI ZwQueryInformationThread(
	__in HANDLE ThreadHandle,
	__in THREADINFOCLASS ThreadInformationClass,
	__out_bcount(ThreadInformationLength) PVOID ThreadInformation,
	__in ULONG ThreadInformationLength,
	__out_opt PULONG ReturnLength
);

VOID checkRemoteInject(HANDLE ThreadId)
{
	auto status = STATUS_SUCCESS;
	PETHREAD thread = NULL;
	status = PsLookupThreadByThreadId(ThreadId, &thread);
	if (!NT_SUCCESS(status))
	{
		return;
	}
	ObDereferenceObject(thread);
	PEPROCESS process = PsGetThreadProcess(thread);
	if (!process)
	{
		return;
	}
	HANDLE pid = PsGetProcessId(process);

	PUNICODE_STRING name = { 0 };
	status = SeLocateProcessImageName(process, &name);
	if (!NT_SUCCESS(status))
	{
		return;
	}

	PEPROCESS injectProcess = PsGetCurrentProcess();
	if (!injectProcess)
	{
		return;
	}
	HANDLE injectPid = PsGetProcessId(injectProcess);

	PUNICODE_STRING injectName = { 0 };
	SeLocateProcessImageName(injectProcess, &injectName);

	if (pid != injectPid)
	{
		DbgPrintEx(77, 0, "发现远程线程创建，进程[%d][%wZ] 在进程[%d][%wZ] 中创建线程 %d \n", injectPid, injectName, pid, name, ThreadId);
	}

	ExFreePoolWithTag(name, 0);
	ExFreePoolWithTag(injectName, 0);
}

VOID threadNotify(
	_In_ HANDLE ProcessId,
	_In_ HANDLE ThreadId,
	_In_ BOOLEAN Create
)
{
	if (Create)
	{
		PLIST_ENTRY next = g_ThreadMonList.head.Flink;

		while (next != &g_ThreadMonList.head)
		{
			PThreadMonItem item = CONTAINING_RECORD(next, ThreadMonItem, entry);

			if (item->pid == HandleToULong(ProcessId))
			{
				DbgPrintEx(77, 0, "发现进程 pid = %d 进行监控 \n", ProcessId);

				checkRemoteInject(ThreadId);
				break;
			}

			next = next->Flink;
		}
	}
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

VOID addPidToList(ULONG pid)
{
	AutoLock<FastMutex> lock(g_ThreadMonList.mutex);

	PThreadMonItem item = (PThreadMonItem)ExAllocatePoolWithTag(PagedPool, sizeof(ThreadMonItem), DRIVER_TAG);
	if (item == nullptr)
	{
		return;
	}

	item->pid = pid;

	InsertTailList(&g_ThreadMonList.head, &item->entry);

	DbgPrintEx(77, 0, "添加 pid = %d 进列表 \n", pid);
}

VOID delPidFromList(ULONG pid)
{
	AutoLock<FastMutex> lock(g_ThreadMonList.mutex);

	PLIST_ENTRY next = g_ThreadMonList.head.Flink;
	PLIST_ENTRY find = NULL;

	while (next != &g_ThreadMonList.head)
	{
		PThreadMonItem item = CONTAINING_RECORD(next, ThreadMonItem, entry);

		if (item->pid == pid)
		{
			find = next;
			break;
		}

		next = next->Flink;
	}

	if (find)
	{
		RemoveEntryList(find);
		ExFreePoolWithTag(CONTAINING_RECORD(find, ThreadMonItem, entry), DRIVER_TAG);

		DbgPrintEx(77, 0, "从列表删除 pid = %d \n", pid);
	}
}

NTSTATUS write(
	_In_ struct _DEVICE_OBJECT* DeviceObject,
	_Inout_ struct _IRP* Irp
)
{
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

			addPidToList(package->pid);

			break;
		}
		case Type::TYPE_DEL:
		{

			delPidFromList(package->pid);

			break;
		}
		default:
		{
			status = STATUS_INVALID_PARAMETER;
			break;
		}
		}
	}

	Irp->IoStatus.Status = status;
	Irp->IoStatus.Information = count;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	return status;
}

VOID DriverUnload(PDRIVER_OBJECT DriverObject)
{
	UP(DriverObject);

	PsRemoveCreateThreadNotifyRoutine(threadNotify);

	UNICODE_STRING linkName = { 0 };
	RtlInitUnicodeString(&linkName, L"\\??\\RemoteThreadMon");
	IoDeleteSymbolicLink(&linkName);

	if (DriverObject->DeviceObject)
	{
		IoDeleteDevice(DriverObject->DeviceObject);
	}

	while (!IsListEmpty(&g_ThreadMonList.head))
	{
		auto entry = RemoveHeadList(&g_ThreadMonList.head);

		PThreadMonItem item = CONTAINING_RECORD(entry, ThreadMonItem, entry);

		ExFreePoolWithTag(item, DRIVER_TAG);
	}

	DbgPrintEx(77, 0, "DriverUnload \r\n");
}

extern "C" NTSTATUS DriverEntry(PDRIVER_OBJECT driver, PUNICODE_STRING reg)
{
	UP(reg);

	auto status = STATUS_SUCCESS;

	g_ThreadMonList.mutex.Init();

	InitializeListHead(&g_ThreadMonList.head);

	PDEVICE_OBJECT devObj = NULL;
	UNICODE_STRING devName = { 0 };
	UNICODE_STRING linkName = { 0 };
	BOOLEAN linkCreate = FALSE;

	RtlInitUnicodeString(&devName, L"\\Device\\RemoteThreadMon");
	RtlInitUnicodeString(&linkName, L"\\??\\RemoteThreadMon");

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
		linkCreate = TRUE;

		status = PsSetCreateThreadNotifyRoutine(threadNotify);
		if (!NT_SUCCESS(status))
		{
			DbgPrintEx(77, 0, "PsSetCreateThreadNotifyRoutine = %x \n", status);
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

	DbgPrintEx(77, 0, "status = %x \n", status);

	driver->MajorFunction[IRP_MJ_CREATE] = createClose;
	driver->MajorFunction[IRP_MJ_CLOSE] = createClose;
	driver->MajorFunction[IRP_MJ_WRITE] = write;

	driver->DriverUnload = DriverUnload;
	return STATUS_SUCCESS;
}