#include <ntifs.h>
#include <ntddk.h>
#include <intrin.h>
#include <fltkernel.h>

#define UP(P) UNREFERENCED_PARAMETER(P)

#pragma comment(lib, "FltMgr.lib")

VOID DriverUnload(PDRIVER_OBJECT DriverObject)
{
	UP(DriverObject);

	DbgPrintEx(77, 0, "DriverUnload \r\n");
}

FLT_PREOP_CALLBACK_STATUS FLTAPI preWriteOper(
	_Inout_ PFLT_CALLBACK_DATA Data,
	_In_ PCFLT_RELATED_OBJECTS FltObjects,
	_Outptr_result_maybenull_ PVOID* CompletionContext
	)
{
	UP(Data);
	UP(FltObjects);
	UP(CompletionContext);

	return FLT_PREOP_COMPLETE;
}

FLT_POSTOP_CALLBACK_STATUS FLTAPI postCreateOper(
	_Inout_ PFLT_CALLBACK_DATA Data,
	_In_ PCFLT_RELATED_OBJECTS FltObjects,
	_In_opt_ PVOID CompletionContext,
	_In_ FLT_POST_OPERATION_FLAGS Flags
	)
{
	UP(Data);
	UP(FltObjects);
	UP(CompletionContext);
	UP(Flags);

	return FLT_POSTOP_FINISHED_PROCESSING;
}

FLT_POSTOP_CALLBACK_STATUS FLTAPI postCloseOper(
	_Inout_ PFLT_CALLBACK_DATA Data,
	_In_ PCFLT_RELATED_OBJECTS FltObjects,
	_In_opt_ PVOID CompletionContext,
	_In_ FLT_POST_OPERATION_FLAGS Flags
)
{
	UP(Data);
	UP(FltObjects);
	UP(CompletionContext);
	UP(Flags);

	return FLT_POSTOP_FINISHED_PROCESSING;
}

NTSTATUS FLTAPI FilterUnloadCallback(
	FLT_FILTER_UNLOAD_FLAGS Flags
	)
{
	UP(Flags);

	return STATUS_SUCCESS;
}

NTSTATUS FLTAPI InstanceSetupCallback(
	_In_ PCFLT_RELATED_OBJECTS FltObjects,
	_In_ FLT_INSTANCE_SETUP_FLAGS Flags,
	_In_ DEVICE_TYPE VolumeDeviceType,
	_In_ FLT_FILESYSTEM_TYPE VolumeFilesystemType
)
{
	UP(FltObjects);
	UP(Flags);
	UP(VolumeDeviceType);
	UP(VolumeFilesystemType);

	return STATUS_SUCCESS;
}

NTSTATUS FLTAPI InstanceQueryTeardownCallback(
	_In_ PCFLT_RELATED_OBJECTS FltObjects,
	_In_ FLT_INSTANCE_QUERY_TEARDOWN_FLAGS Flags
)
{
	UP(FltObjects);
	UP(Flags);

	return STATUS_SUCCESS;
}


VOID FLTAPI InstanceTeardownStartCallback(
	_In_ PCFLT_RELATED_OBJECTS FltObjects,
	_In_ FLT_INSTANCE_TEARDOWN_FLAGS Reason
)
{
	UP(FltObjects);
	UP(Reason);
}

VOID FLTAPI InstanceTeardownCompleteCallback(
	_In_ PCFLT_RELATED_OBJECTS FltObjects,
	_In_ FLT_INSTANCE_TEARDOWN_FLAGS Reason
	)
{
	UP(FltObjects);
	UP(Reason);
}

const FLT_OPERATION_REGISTRATION callbacks[] = {
	{
		IRP_MJ_CREATE,
		0,
		nullptr,
		postCreateOper //�����ļ���Ŀ¼��ص�
	},
	{
		IRP_MJ_WRITE,
		FLTFL_OPERATION_REGISTRATION_SKIP_PAGING_IO, //�Ի�ҳI/O�����ûص�����
		preWriteOper, //д֮ǰ�ص�
		nullptr
	},
	{
		IRP_MJ_CLOSE,
		0,
		nullptr,
		postCloseOper //�رպ�ص�
	},
	{
		IRP_MJ_OPERATION_END
	}
};

const FLT_REGISTRATION filterReg = {
	sizeof(FLT_REGISTRATION),
	FLT_REGISTRATION_VERSION,
	0, //flags
	nullptr, //Context
	callbacks,
	FilterUnloadCallback,
	InstanceSetupCallback,
	InstanceQueryTeardownCallback,
	InstanceTeardownStartCallback,
	InstanceTeardownCompleteCallback
};

PFLT_FILTER filterHandle;

/**
 * https://learn.microsoft.com/en-us/windows-hardware/drivers/ifs/allocated-altitudes
 * 
 * С���������ĸ߶�ֵ�൱�ؼ�
 */
extern "C" NTSTATUS DriverEntry(PDRIVER_OBJECT driver, PUNICODE_STRING reg)
{
	UP(reg);

	auto status = STATUS_SUCCESS;

	//ע�����
	status = FltRegisterFilter(driver, &filterReg, &filterHandle);
	if (NT_SUCCESS(status))
	{
		//����IO����
		status = FltStartFiltering(filterHandle);
		if (!NT_SUCCESS(status))
		{
			FltUnregisterFilter(filterHandle);
		}
	}

	return status;
}