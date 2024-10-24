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
 * 完成请求
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

	//将锁定的缓冲区映射到系统空间
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
 HKLM\System\CurrentControlSet\Control\Class下面列出的ClassGuid值。
 值的名称是LowerFilters，它是一个包含服务名称的多字符串值，指向同样的Services键。
 上层过滤器以类似的方式进行搜索，但是值的名称是UpperFilters。

 IRP处理流程

 1、将请求向下传递
 2、完全处理此IRP，最终将调用IoCompleteRequest
 3、（1）和（2）的混合，驱动程序检查IRP，做一些事情（比如记录下请求），然后将它往下传递。
 4、往下传递请求，并在下层设备完成请求时得到通知，IoSetCompletion
 5、开始某种异步IRP处理

 缓冲I/O
 driver->Flags |= DO_BUFFERED_IO;
 使用 Irp->AssociatedIrp->SystemBuffer;

 直接I/O
 driver->Flags |= DO_DIRECT_IO;
 使用 Irp->MdlAddress

 无I/O（Neither I/O）方式
 单纯表示驱动程序不会从I/O管理器得到任何帮助，怎么处理用户缓冲区完全取决于驱动程序自身。
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