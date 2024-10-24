#include <ntifs.h>
#include <ntddk.h>
#include <intrin.h>
#include "ThreadPriority.h"

#define UP(P) UNREFERENCED_PARAMETER(P)

VOID DriverUnload(PDRIVER_OBJECT DriverObject)
{
	UP(DriverObject);

	//ɾ������
	UNICODE_STRING linkName = { 0 };
	RtlInitUnicodeString(&linkName, L"\\??\\ThreadPriority");
	IoDeleteSymbolicLink(&linkName);

	//ɾ���豸
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

	Irp->IoStatus.Status = STATUS_SUCCESS; //ָ����ʲô״̬��ɴ�����
	Irp->IoStatus.Information = 0;
	//�����IRP���ͻ����Ĵ����ߣ�ͨ����I/O����������Ȼ�������֪ͨ�ͻ������������ɡ�
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

		//��ȡ�̵߳�ETHREAD
		PETHREAD thread = NULL;
		status = PsLookupThreadByThreadId((HANDLE)data->threadId, &thread);
		if (!NT_SUCCESS(status))
		{
			break;
		}
		ObDereferenceObject(thread);

		//�����߳����ȼ�
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

	//�����豸
	NTSTATUS status = IoCreateDevice(driver, 0, &devName, FILE_DEVICE_UNKNOWN, 0, FALSE, &devObj);
	if (!NT_SUCCESS(status))
	{
		DbgPrintEx(77, 0, "�����豸ʧ��\n");
		return status;
	}

	//��ӷ�������
	UNICODE_STRING linkName = { 0 };
	RtlInitUnicodeString(&linkName, L"\\??\\ThreadPriority");
	status = IoCreateSymbolicLink(&linkName, &devName);
	if (!NT_SUCCESS(status))
	{
		DbgPrintEx(77, 0, "��������ʧ��\n");
		return status;
	}


	//������Ҫ֧�ֵķַ�����
	driver->MajorFunction[IRP_MJ_CREATE] = createClose;
	driver->MajorFunction[IRP_MJ_CLOSE] = createClose;
	driver->MajorFunction[IRP_MJ_DEVICE_CONTROL] = ctl;

	driver->DriverUnload = DriverUnload;
	return STATUS_SUCCESS;
}