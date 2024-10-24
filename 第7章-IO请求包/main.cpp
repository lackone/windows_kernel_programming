#include <ntifs.h>
#include <ntddk.h>
#include <intrin.h>

#define UP(P) UNREFERENCED_PARAMETER(P)

VOID DriverUnload(PDRIVER_OBJECT DriverObject)
{
	UP(DriverObject);

	UNICODE_STRING linkName = { 0 };
	RtlInitUnicodeString(&linkName, L"\\??\\Zero");
	IoDeleteSymbolicLink(&linkName);

	if (DriverObject->DeviceObject)
	{
		IoDeleteDevice(DriverObject->DeviceObject);
	}

	DbgPrintEx(77, 0, "DriverUnload \r\n");
}

/**
 * �������
 */
NTSTATUS CompleteRequest(PIRP irp, NTSTATUS status = STATUS_SUCCESS, ULONG_PTR info = 0)
{
	irp->IoStatus.Status = status;
	irp->IoStatus.Information = info;
	IoCompleteRequest(irp, IO_NO_INCREMENT);
	return status;
}

NTSTATUS createClose(
	_In_ struct _DEVICE_OBJECT* DeviceObject,
	_Inout_ struct _IRP* Irp
)
{
	UP(DeviceObject);
	DbgPrintEx(77, 0, "createClose \n");
	return CompleteRequest(Irp);
}

NTSTATUS read(
	_In_ struct _DEVICE_OBJECT* DeviceObject,
	_Inout_ struct _IRP* Irp
)
{
	UP(DeviceObject);
	auto stack = IoGetCurrentIrpStackLocation(Irp);
	auto len = stack->Parameters.Read.Length;

	if (len == 0)
	{
		return CompleteRequest(Irp, STATUS_INVALID_BUFFER_SIZE);
	}

	//�������Ļ�����ӳ�䵽ϵͳ�ռ�
	auto buf = MmGetSystemAddressForMdlSafe(Irp->MdlAddress, NormalPagePriority);
	if (!buf)
	{
		return CompleteRequest(Irp, STATUS_INSUFFICIENT_RESOURCES);
	}

	memset(buf, 0, len);

	return CompleteRequest(Irp, STATUS_SUCCESS, len);
}

NTSTATUS write(
	_In_ struct _DEVICE_OBJECT* DeviceObject,
	_Inout_ struct _IRP* Irp
)
{
	UP(DeviceObject);
	auto stack = IoGetCurrentIrpStackLocation(Irp);
	auto len = stack->Parameters.Write.Length;

	return CompleteRequest(Irp, STATUS_SUCCESS, len);
}

/**
 * 
 HKLM\System\CurrentControlSet\Control\Class�����г���ClassGuidֵ��
 ֵ��������LowerFilters������һ�������������ƵĶ��ַ���ֵ��ָ��ͬ����Services����
 �ϲ�����������Ƶķ�ʽ��������������ֵ��������UpperFilters��

 IRP��������

 1�����������´���
 2����ȫ�����IRP�����ս�����IoCompleteRequest
 3����1���ͣ�2���Ļ�ϣ�����������IRP����һЩ���飨�����¼�����󣩣�Ȼ�������´��ݡ�
 4�����´������󣬲����²��豸�������ʱ�õ�֪ͨ��IoSetCompletion
 5����ʼĳ���첽IRP����

 ����I/O
 driver->Flags |= DO_BUFFERED_IO;
 ʹ�� Irp->AssociatedIrp->SystemBuffer;

 ֱ��I/O
 driver->Flags |= DO_DIRECT_IO;
 ʹ�� Irp->MdlAddress

 ��I/O��Neither I/O����ʽ
 ������ʾ�������򲻻��I/O�������õ��κΰ�������ô�����û���������ȫȡ����������������
 */
extern "C" NTSTATUS DriverEntry(PDRIVER_OBJECT driver, PUNICODE_STRING reg)
{
	UP(reg);

	UNICODE_STRING devName = { 0 };
	RtlInitUnicodeString(&devName, L"\\Device\\Zero");
	UNICODE_STRING linkName = { 0 };
	RtlInitUnicodeString(&linkName, L"\\??\\Zero");

	auto status = STATUS_SUCCESS;

	PDEVICE_OBJECT devObj = NULL;

	do 
	{
		status = IoCreateDevice(driver, 0, &devName, FILE_DEVICE_UNKNOWN, 0, FALSE, &devObj);
		if (!NT_SUCCESS(status))
		{
			DbgPrintEx(77, 0, "IoCreateDevice = %x \n", status);
			break;
		}

		status = IoCreateSymbolicLink(&linkName, &devName);
		if (!NT_SUCCESS(status))
		{
			DbgPrintEx(77, 0, "IoCreateSymbolicLink = %x \n", status);
			break;
		}

		devObj->Flags |= DO_DIRECT_IO;

		driver->MajorFunction[IRP_MJ_CREATE] = createClose;
		driver->MajorFunction[IRP_MJ_CLOSE] = createClose;
		driver->MajorFunction[IRP_MJ_READ] = read;
		driver->MajorFunction[IRP_MJ_WRITE] = write;

	} while (0);

	driver->DriverUnload = DriverUnload;
	return status;
}