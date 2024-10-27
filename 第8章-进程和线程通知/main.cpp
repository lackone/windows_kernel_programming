#include <ntifs.h>
#include <ntddk.h>
#include <intrin.h>
#include "Common.h"

#define UP(P) UNREFERENCED_PARAMETER(P)

#define DRIVER_TAG 'mon'

Globals g_Globals;

VOID PushItem(PLIST_ENTRY entry)
{
	AutoLock<FastMutex> lock(g_Globals.mutex);

	if (g_Globals.itemCount > 1024)
	{
		//如果超出1024个，则从头部删除，并释放内存
		auto head = RemoveHeadList(&g_Globals.itemsHead);
		g_Globals.itemCount--;
		auto item = CONTAINING_RECORD(head, FullItem<ItemHeader>, entry);
		ExFreePoolWithTag(item, DRIVER_TAG);
	}

	InsertTailList(&g_Globals.itemsHead, entry);

	g_Globals.itemCount++;
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
		//进程创建
		USHORT size = sizeof(FullItem<ProcessCreateInfo>);
		USHORT cmdSize = 0;
		USHORT imgSize = 0;
		if (CreateInfo->CommandLine)
		{
			cmdSize = CreateInfo->CommandLine->Length;
			size += cmdSize;
		}
		if (CreateInfo->ImageFileName)
		{
			imgSize = CreateInfo->ImageFileName->Length;
			size += imgSize;
		}
		auto info = (FullItem<ProcessCreateInfo>*)ExAllocatePoolWithTag(PagedPool, size, DRIVER_TAG);
		if (info == nullptr)
		{
			DbgPrintEx(77, 0, "ExAllocatePoolWithTag error \n");
			return;
		}
		auto& item = info->data;

		//获取当前系统时间
		KeQuerySystemTimePrecise(&item.time);
		item.type = ItemType::ProcessCreate;
		item.size = sizeof(ProcessCreateInfo) + cmdSize + imgSize;
		item.pid = HandleToLong(ProcessId);
		item.ppid = HandleToLong(CreateInfo->ParentProcessId);

		if (cmdSize > 0)
		{
			item.cmdLen = cmdSize / sizeof(WCHAR);
			item.cmdOffset = sizeof(item);

			memcpy((PUCHAR)&item + item.cmdOffset, CreateInfo->CommandLine->Buffer, cmdSize);
		}
		else
		{
			item.cmdLen = 0;
		}

		if (imgSize > 0)
		{
			item.imgLen = imgSize / sizeof(WCHAR);
			item.imgOffset = sizeof(item) + cmdSize;

			memcpy((PUCHAR)&item + item.imgOffset, CreateInfo->ImageFileName->Buffer, imgSize);
		}
		else
		{
			item.imgLen = 0;
		}

		PushItem(&info->entry);
	}
	else
	{
		//进程退出
		auto info = (FullItem<ProcessExitInfo>*)ExAllocatePoolWithTag(PagedPool, sizeof(FullItem<ProcessExitInfo>), DRIVER_TAG);
		if (info == nullptr)
		{
			DbgPrintEx(77, 0, "ExAllocatePoolWithTag error \n");
			return;
		}
		auto& item = info->data;

		//获取当前系统时间
		KeQuerySystemTimePrecise(&item.time);
		item.type = ItemType::ProcessExit;
		item.pid = HandleToLong(ProcessId);
		item.size = sizeof(ProcessExitInfo);

		PushItem(&info->entry);
	}
}

VOID threadNotify(
	_In_ HANDLE ProcessId,
	_In_ HANDLE ThreadId,
	_In_ BOOLEAN Create
	)
{
	auto size = sizeof(FullItem<ThreadCreateExitInfo>);
	auto info = (FullItem<ThreadCreateExitInfo>*)ExAllocatePoolWithTag(PagedPool, size, DRIVER_TAG);
	if (info == nullptr)
	{
		DbgPrintEx(77, 0, "ExAllocatePoolWithTag error \n");
		return;
	}

	auto& item = info->data;
	KeQuerySystemTimePrecise(&item.time);
	item.size = (USHORT)size;
	item.type = Create ? ItemType::ThreadCreate : ItemType::ThreadExit;
	item.pid = HandleToLong(ProcessId);
	item.tid = HandleToLong(ThreadId);

	PushItem(&info->entry);
}

VOID imageNotify(
	_In_opt_ PUNICODE_STRING FullImageName,
	_In_ HANDLE ProcessId,                // pid into which image is being mapped
	_In_ PIMAGE_INFO ImageInfo
	)
{
	auto size = sizeof(FullItem<ImageLoadInfo>);
	auto pathSize = 0;
	if (FullImageName && FullImageName->Length > 0)
	{
		pathSize = FullImageName->Length;
		size += pathSize;
	}
	auto info = (FullItem<ImageLoadInfo>*)ExAllocatePoolWithTag(PagedPool, size, DRIVER_TAG);
	if (info == nullptr)
	{
		DbgPrintEx(77, 0, "ExAllocatePoolWithTag error \n");
		return;
	}

	auto& item = info->data;
	KeQuerySystemTimePrecise(&item.time);
	item.size = (USHORT)(sizeof(item) + pathSize);
	item.type = ItemType::ImageLoad;
	item.pid = HandleToLong(ProcessId);
	item.imageBase = (ULONG_PTR)ImageInfo->ImageBase;
	item.imageSize = (ULONG_PTR)ImageInfo->ImageSize;
	item.pathLen = (USHORT)(pathSize / sizeof(WCHAR));
	item.pathOffset = sizeof(item);

	if (pathSize > 0)
	{
		memcpy((PUCHAR)&item + item.pathOffset, FullImageName->Buffer, pathSize);
	}

	PushItem(&info->entry);
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

NTSTATUS read(
	_In_ struct _DEVICE_OBJECT* DeviceObject,
	_Inout_ struct _IRP* Irp
)
{
	UP(DeviceObject);
	auto stack = IoGetCurrentIrpStackLocation(Irp);
	auto len = stack->Parameters.Read.Length;
	auto status = STATUS_SUCCESS;
	auto count = 0;

	auto buf = (PUCHAR)MmGetSystemAddressForMdlSafe(Irp->MdlAddress, NormalPagePriority);
	if (!buf)
	{
		status = STATUS_INSUFFICIENT_RESOURCES;
	}
	else
	{
		AutoLock<FastMutex> lock(g_Globals.mutex);

		while (true)
		{
			if (IsListEmpty(&g_Globals.itemsHead))
			{
				break;
			}


			auto entry = RemoveHeadList(&g_Globals.itemsHead);
			auto info = CONTAINING_RECORD(entry, FullItem<ItemHeader>, entry);
			auto size = info->data.size;

			if (len < size)
			{
				InsertHeadList(&g_Globals.itemsHead, entry);
				break;
			}

			g_Globals.itemCount--;

			//拷贝数据
			::memcpy(buf, &info->data, size);

			len -= size;
			buf += size;
			count += size;

			ExFreePoolWithTag(info, DRIVER_TAG);
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

	PsSetCreateProcessNotifyRoutineEx(ProcessNotify, TRUE);

	PsRemoveCreateThreadNotifyRoutine(threadNotify);

	PsRemoveLoadImageNotifyRoutine(imageNotify);

	UNICODE_STRING linkName = { 0 };
	RtlInitUnicodeString(&linkName, L"\\??\\sysmon");
	IoDeleteSymbolicLink(&linkName);

	if (DriverObject->DeviceObject)
	{
		IoDeleteDevice(DriverObject->DeviceObject);
	}

	while (!IsListEmpty(&g_Globals.itemsHead))
	{
		auto entry = RemoveHeadList(&g_Globals.itemsHead);
		ExFreePoolWithTag(CONTAINING_RECORD(entry, FullItem<ItemHeader>, entry), DRIVER_TAG);
	}

	DbgPrintEx(77, 0, "DriverUnload \r\n");
}

/**
 * 
用到这些回调的驱动程序必须在其可移动执行文件（PortableExecutable，PE）映像头中设置有IMAGE_DLLCHARACTERISTICS_FORCE_INTEGRITY标志。
没有这个标志的话，调用注册函数会返回STATUS_ACCESS_DENIED（与驱动程序测试签名模式无关）。
当前，Visual Studio没有为设置此标志提供界面，它必须通过链接器命令行参数/integritycheck进行设置。
 */
extern "C" NTSTATUS DriverEntry(PDRIVER_OBJECT driver, PUNICODE_STRING reg)
{
	UP(reg);

	auto status = STATUS_SUCCESS;

	//初始化链表头
	InitializeListHead(&g_Globals.itemsHead);
	g_Globals.mutex.Init();

	PDEVICE_OBJECT devObj = NULL;
	UNICODE_STRING devName = { 0 };
	UNICODE_STRING linkName = { 0 };
	BOOLEAN linkCreate = FALSE;

	RtlInitUnicodeString(&devName, L"\\Device\\sysmon");
	RtlInitUnicodeString(&linkName, L"\\??\\sysmon");

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

		status = PsSetCreateProcessNotifyRoutineEx(ProcessNotify, FALSE);
		if (!NT_SUCCESS(status))
		{
			DbgPrintEx(77, 0, "PsSetCreateProcessNotifyRoutineEx = %x \n", status);
			break;
		}

		status = PsSetCreateThreadNotifyRoutine(threadNotify);
		if (!NT_SUCCESS(status))
		{
			DbgPrintEx(77, 0, "PsSetCreateThreadNotifyRoutine = %x \n", status);
			break;
		}

		status = PsSetLoadImageNotifyRoutine(imageNotify);
		if (!NT_SUCCESS(status))
		{
			DbgPrintEx(77, 0, "PsSetLoadImageNotifyRoutine = %x \n", status);
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
	driver->MajorFunction[IRP_MJ_READ] = read;

	driver->DriverUnload = DriverUnload;
	return STATUS_SUCCESS;
}