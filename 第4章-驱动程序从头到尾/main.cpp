#include <ntifs.h>
#include <ntddk.h>
#include <intrin.h>
#include "ThreadPriority.h"

#define UP(P) UNREFERENCED_PARAMETER(P)

VOID DriverUnload(PDRIVER_OBJECT DriverObject)
{
	UP(DriverObject);

	//删除链接
	UNICODE_STRING linkName = { 0 };
	RtlInitUnicodeString(&linkName, L"\\??\\ThreadPriority");
	IoDeleteSymbolicLink(&linkName);

	//删除设备
	if (DriverObject->DeviceObject)
	{
		IoDeleteDevice(DriverObject->DeviceObject);
	}

	DbgPrintEx(77, 0, "DriverUnload \r\n");
}

NTSTATUS createClose(
	_In_ struct _DEVICE_OBJECT* DeviceObject,
	_Inout_ struct _IRP* Irp
)
{
	UP(DeviceObject);

	Irp->IoStatus.Status = STATUS_SUCCESS; //指明用什么状态完成此请求。
	Irp->IoStatus.Information = 0;
	//它会把IRP传送回它的创建者（通常是I/O管理器），然后管理器通知客户程序操作已完成。
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

	switch (stack->Parameters.DeviceIoControl.IoControlCode)
	{
	case SET_THREAD_PRIORITY:
	{
		if (stack->Parameters.DeviceIoControl.InputBufferLength < sizeof(THREAD_DATA))
		{
			status = STATUS_BUFFER_TOO_SMALL;
			break;
		}

		auto data = (THREAD_DATA*)stack->Parameters.DeviceIoControl.Type3InputBuffer;

		if (data == NULL)
		{
			status = STATUS_INVALID_PARAMETER;
			break;
		}

		if (data->priority < 1 || data->priority > 31)
		{
			status = STATUS_INVALID_PARAMETER;
			break;
		}

		//获取线程的ETHREAD
		PETHREAD thread = NULL;
		status = PsLookupThreadByThreadId((HANDLE)data->threadId, &thread);
		if (!NT_SUCCESS(status))
		{
			break;
		}
		ObDereferenceObject(thread);

		//设置线程优先级
		KeSetPriorityThread((PKTHREAD)thread, data->priority);

		DbgPrintEx(77, 0, "set Priority %d %d \n", data->threadId, data->priority);
	}
		break;
	default:
		status = STATUS_INVALID_DEVICE_REQUEST;
		break;
	}

	Irp->IoStatus.Status = status;
	Irp->IoStatus.Information = 0;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	return status;
}

extern "C" NTSTATUS DriverEntry(PDRIVER_OBJECT driver, PUNICODE_STRING reg)
{
	UP(reg);

	UNICODE_STRING devName = { 0 };
	RtlInitUnicodeString(&devName, L"\\Device\\ThreadPriority");

	PDEVICE_OBJECT devObj = NULL;

	//创建设备
	NTSTATUS status = IoCreateDevice(driver, 0, &devName, FILE_DEVICE_UNKNOWN, 0, FALSE, &devObj);
	if (!NT_SUCCESS(status))
	{
		DbgPrintEx(77, 0, "创建设备失败\n");
		return status;
	}

	//添加符号链接
	UNICODE_STRING linkName = { 0 };
	RtlInitUnicodeString(&linkName, L"\\??\\ThreadPriority");
	status = IoCreateSymbolicLink(&linkName, &devName);
	if (!NT_SUCCESS(status))
	{
		DbgPrintEx(77, 0, "创建链接失败\n");
		return status;
	}


	//设置需要支持的分发例程
	driver->MajorFunction[IRP_MJ_CREATE] = createClose;
	driver->MajorFunction[IRP_MJ_CLOSE] = createClose;
	driver->MajorFunction[IRP_MJ_DEVICE_CONTROL] = ctl;

	driver->DriverUnload = DriverUnload;
	return STATUS_SUCCESS;
}