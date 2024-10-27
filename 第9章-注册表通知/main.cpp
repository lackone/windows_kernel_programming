#include <ntifs.h>
#include <ntddk.h>
#include <intrin.h>
#include "Common.h"

#define UP(P) UNREFERENCED_PARAMETER(P)

#define DRIVER_TAG 'mon'

#define MACHINE L"\\REGISTRY\\MACHINE"

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

	CmUnRegisterCallback(g_Globals.regCookie);

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

NTSTATUS RegNotify(
	_In_ PVOID CallbackContext,
	_In_opt_ PVOID Argument1,
	_In_opt_ PVOID Argument2
)
{
	UP(CallbackContext);

	switch ((REG_NOTIFY_CLASS)(ULONG_PTR)Argument1)
	{
	case RegNtPostSetValueKey:
	{
		auto args = (REG_POST_OPERATION_INFORMATION*)Argument2;
		if (!args)
		{
			break;
		}
		if (!NT_SUCCESS(args->Status))
		{
			break;
		}

		PCUNICODE_STRING name = { 0 };
		if (NT_SUCCESS(CmCallbackGetKeyObjectIDEx(&g_Globals.regCookie, args->Object, nullptr, &name, 0)))
		{
			if (wcsncmp(name->Buffer, MACHINE, wcslen(MACHINE)) == 0)
			{
				auto preInfo = (REG_SET_VALUE_KEY_INFORMATION*)args->PreInformation;

				auto size = sizeof(FullItem<RegSetValueInfo>);
				auto info = (FullItem<RegSetValueInfo>*)ExAllocatePoolWithTag(PagedPool, size, DRIVER_TAG);
				if (info == nullptr)
				{
					break;
				}
				RtlZeroMemory(info, size);

				auto& item = info->data;

				KeQuerySystemTimePrecise(&item.time);
				item.size = sizeof(item);
				item.type = ItemType::RegSetValue;
				item.pid = HandleToULong(PsGetCurrentProcessId());
				item.tid = HandleToULong(PsGetCurrentThreadId());

				wcsncpy_s(item.keyName, name->Buffer, name->Length / sizeof(WCHAR) - 1);
				wcsncpy_s(item.valueName, preInfo->ValueName->Buffer, preInfo->ValueName->Length / sizeof(WCHAR) - 1);

				item.dataType = preInfo->Type;
				item.dataSize = preInfo->DataSize;

				memcpy(item.data, preInfo->Data, min(item.dataSize, sizeof(item.data)));

				PushItem(&info->entry);
			}

			CmCallbackReleaseKeyObjectIDEx(name);
		}

		break;
	}
	}


	return STATUS_SUCCESS;
}

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
		
		UNICODE_STRING altitude = RTL_CONSTANT_STRING(L"123456.78");
		status = CmRegisterCallbackEx(RegNotify, &altitude, driver, nullptr, &g_Globals.regCookie, nullptr);
		if (!NT_SUCCESS(status))
		{
			DbgPrintEx(77, 0, "CmRegisterCallbackEx = %x \n", status);
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