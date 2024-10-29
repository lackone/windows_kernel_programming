#include <ntifs.h>
#include <ntddk.h>
#include <intrin.h>
#include "DeviceMonManager.h"

#define UP(P) UNREFERENCED_PARAMETER(P)

#define ADD_DEVICE CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define REMOVE_DEVICE CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define REMOVE_ALL CTL_CODE(FILE_DEVICE_UNKNOWN, 0x802, METHOD_NEITHER, FILE_ANY_ACCESS)

DeviceMonManager g_DevMonManager;

NTSTATUS CompleteRequest(PIRP Irp, NTSTATUS status = STATUS_SUCCESS, ULONG_PTR information = 0)
{
	Irp->IoStatus.Status = status;
	Irp->IoStatus.Information = information;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	return status;
}

PCSTR MajorFunctionToString(UCHAR major) 
{
	static const char* strings[] = 
	{
		"IRP_MJ_CREATE",
		"IRP_MJ_CREATE_NAMED_PIPE",
		"IRP_MJ_CLOSE",
		"IRP_MJ_READ",
		"IRP_MJ_WRITE",
		"IRP_MJ_QUERY_INFORMATION",
		"IRP_MJ_SET_INFORMATION",
		"IRP_MJ_QUERY_EA",
		"IRP_MJ_SET_EA",
		"IRP_MJ_FLUSH_BUFFERS",
		"IRP_MJ_QUERY_VOLUME_INFORMATION",
		"IRP_MJ_SET_VOLUME_INFORMATION",
		"IRP_MJ_DIRECTORY_CONTROL",
		"IRP_MJ_FILE_SYSTEM_CONTROL",
		"IRP_MJ_DEVICE_CONTROL",
		"IRP_MJ_INTERNAL_DEVICE_CONTROL",
		"IRP_MJ_SHUTDOWN",
		"IRP_MJ_LOCK_CONTROL",
		"IRP_MJ_CLEANUP",
		"IRP_MJ_CREATE_MAILSLOT",
		"IRP_MJ_QUERY_SECURITY",
		"IRP_MJ_SET_SECURITY",
		"IRP_MJ_POWER",
		"IRP_MJ_SYSTEM_CONTROL",
		"IRP_MJ_DEVICE_CHANGE",
		"IRP_MJ_QUERY_QUOTA",
		"IRP_MJ_SET_QUOTA",
		"IRP_MJ_PNP",
	};
	return strings[major];
}

VOID DriverUnload(PDRIVER_OBJECT DriverObject)
{
	UP(DriverObject);

	UNICODE_STRING linkName = RTL_CONSTANT_STRING(L"\\??\\DevMonManager");
	IoDeleteSymbolicLink(&linkName);

	if (g_DevMonManager._cdo)
	{
		IoDeleteDevice(g_DevMonManager._cdo);
	}

	g_DevMonManager.RemoveAllDevice();

	DbgPrintEx(77, 0, "DriverUnload \r\n");
}

NTSTATUS DeviceCtl(
	_In_ struct _DEVICE_OBJECT* DeviceObject,
	_Inout_ struct _IRP* Irp
)
{
	UP(DeviceObject);
	auto stack = IoGetCurrentIrpStackLocation(Irp);
	auto status = STATUS_INVALID_DEVICE_REQUEST;
	auto code = stack->Parameters.DeviceIoControl.IoControlCode;

	switch (code) 
	{
	case ADD_DEVICE:
	case REMOVE_DEVICE:
	{
		auto buf = (WCHAR*)Irp->AssociatedIrp.SystemBuffer;
		auto len = stack->Parameters.DeviceIoControl.InputBufferLength;

		if (buf == nullptr || len > 512)
		{
			status = STATUS_INVALID_BUFFER_SIZE;
			break;
		}

		buf[len / sizeof(WCHAR) - 1] = L'\0';

		if (code == ADD_DEVICE)
		{
			status = g_DevMonManager.AddDevice(buf);
		}
		else
		{
			auto removed = g_DevMonManager.RemoveDevice(buf);

			status = removed ? STATUS_SUCCESS : STATUS_NOT_FOUND;
		}
		break;
	}

	case REMOVE_ALL:
	{
		g_DevMonManager.RemoveAllDevice();

		status = STATUS_SUCCESS;
		break;
	}
	}

	return CompleteRequest(Irp, status);
}

NTSTATUS filterFunc(
	_In_ struct _DEVICE_OBJECT* DeviceObject,
	_Inout_ struct _IRP* Irp
)
{
	auto stack = IoGetCurrentIrpStackLocation(Irp);

	//判断 目标是被过滤的设备之一或者CDO
	if (DeviceObject == g_DevMonManager._cdo)
	{
		switch (stack->MajorFunction)
		{
		case IRP_MJ_CREATE:
		case IRP_MJ_CLOSE:
			return CompleteRequest(Irp);
		case IRP_MJ_DEVICE_CONTROL:
			return DeviceCtl(DeviceObject, Irp);
		}

		return CompleteRequest(Irp, STATUS_INVALID_DEVICE_REQUEST);
	}

	auto ext = (PDeviceExt)DeviceObject->DeviceExtension;

	auto thread = Irp->Tail.Overlay.Thread;
	HANDLE tid = nullptr, pid = nullptr;
	if (thread)
	{
		tid = PsGetThreadId(thread);
		pid = PsGetThreadProcessId(thread);
	}

	DbgPrintEx(77, 0, "driver %wZ pid %d tid %d MJ=%d %s \n",
		&ext->lowObj->DriverObject->DriverName,
		HandleToULong(pid),
		HandleToULong(tid),
		stack->MajorFunction,
		MajorFunctionToString(stack->MajorFunction)
	);

	IoSkipCurrentIrpStackLocation(Irp);
	return IoCallDriver(ext->lowObj, Irp);
}

NTSTATUS FilterAddDevice(
	_In_ struct _DRIVER_OBJECT* DriverObject,
	_In_ struct _DEVICE_OBJECT* PhysicalDeviceObject
)
{
	PDEVICE_OBJECT filterObj = nullptr;

	auto status = IoCreateDevice(DriverObject, sizeof(DeviceExt), nullptr, FILE_DEVICE_UNKNOWN, 0, FALSE, &filterObj);
	if (!NT_SUCCESS(status))
	{
		return status;
	}

	auto ext = (PDeviceExt)filterObj->DeviceExtension;

	status = IoAttachDeviceToDeviceStackSafe(filterObj, PhysicalDeviceObject, &ext->lowObj);
	if (!NT_SUCCESS(status))
	{
		IoDeleteDevice(filterObj);
		return status;
	}

	filterObj->Flags |= (ext->lowObj->Flags & (DO_BUFFERED_IO | DO_DIRECT_IO));
	filterObj->Flags &= ~DO_DEVICE_INITIALIZING;
	filterObj->Flags |= DO_POWER_PAGABLE;

	filterObj->DeviceType = ext->lowObj->DeviceType;

	return status;
}

extern "C" NTSTATUS DriverEntry(PDRIVER_OBJECT driver, PUNICODE_STRING reg)
{
	UP(reg);

	UNICODE_STRING devName = RTL_CONSTANT_STRING(L"\\Device\\DevMonManager");
	PDEVICE_OBJECT devObj = nullptr;

	auto status = IoCreateDevice(driver, 0, &devName, FILE_DEVICE_UNKNOWN, 0, FALSE, &devObj);
	if (!NT_SUCCESS(status))
	{
		return status;
	}

	UNICODE_STRING linkName = RTL_CONSTANT_STRING(L"\\??\\DevMonManager");
	status = IoCreateSymbolicLink(&linkName, &devName);
	if (!NT_SUCCESS(status))
	{
		IoDeleteDevice(devObj);
		return status;
	}

	for (auto& func : driver->MajorFunction)
	{
		func = filterFunc;
	}

	g_DevMonManager._cdo = devObj;
	g_DevMonManager.Init(driver);

	//这个AddDevice回调函数会在一个新的、属于本驱动程序的硬件设备被即插即用系统标识出来时被调用。
	//driver->DriverExtension->AddDevice = FilterAddDevice;

	driver->DriverUnload = DriverUnload;
	return STATUS_SUCCESS;
}