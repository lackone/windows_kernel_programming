/*++

Module Name:

    Chapter10DelProtect2.c

Abstract:

    This is the main module of the Chapter10_DelProtect2 miniFilter driver.

Environment:

    Kernel mode

--*/

#include <fltKernel.h>
#include <dontuse.h>
#include "Common.h"

#pragma prefast(disable:__WARNING_ENCODE_MEMBER_FUNCTION_POINTER, "Not valid for kernel mode drivers")


PFLT_FILTER gFilterHandle;
ULONG_PTR OperationStatusCtx = 1;

#define PTDBG_TRACE_ROUTINES            0x00000001
#define PTDBG_TRACE_OPERATION_STATUS    0x00000002

ULONG gTraceFlags = 0;

//定义需要用到的变量
const int MaxDirNums = 32;
DirEntry dirNames[MaxDirNums];
int dirNamesCount;
FastMutex dirNamesLock;

#define PT_DBG_PRINT( _dbgLevel, _string )          \
    (FlagOn(gTraceFlags,(_dbgLevel)) ?              \
        DbgPrint _string :                          \
        ((int)0))

/*************************************************************************
    Prototypes
*************************************************************************/

EXTERN_C_START

DRIVER_INITIALIZE DriverEntry;
NTSTATUS
DriverEntry (
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath
    );

NTSTATUS
Chapter10DelProtect2InstanceSetup (
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_ FLT_INSTANCE_SETUP_FLAGS Flags,
    _In_ DEVICE_TYPE VolumeDeviceType,
    _In_ FLT_FILESYSTEM_TYPE VolumeFilesystemType
    );

VOID
Chapter10DelProtect2InstanceTeardownStart (
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_ FLT_INSTANCE_TEARDOWN_FLAGS Flags
    );

VOID
Chapter10DelProtect2InstanceTeardownComplete (
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_ FLT_INSTANCE_TEARDOWN_FLAGS Flags
    );

NTSTATUS
Chapter10DelProtect2Unload (
    _In_ FLT_FILTER_UNLOAD_FLAGS Flags
    );

NTSTATUS
Chapter10DelProtect2InstanceQueryTeardown (
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_ FLT_INSTANCE_QUERY_TEARDOWN_FLAGS Flags
    );

FLT_PREOP_CALLBACK_STATUS
Chapter10DelProtect2PreOperation (
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _Flt_CompletionContext_Outptr_ PVOID *CompletionContext
    );

VOID
Chapter10DelProtect2OperationStatusCallback (
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_ PFLT_IO_PARAMETER_BLOCK ParameterSnapshot,
    _In_ NTSTATUS OperationStatus,
    _In_ PVOID RequesterContext
    );

FLT_POSTOP_CALLBACK_STATUS
Chapter10DelProtect2PostOperation (
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_opt_ PVOID CompletionContext,
    _In_ FLT_POST_OPERATION_FLAGS Flags
    );

FLT_PREOP_CALLBACK_STATUS
Chapter10DelProtect2PreOperationNoPostOperation (
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _Flt_CompletionContext_Outptr_ PVOID *CompletionContext
    );

BOOLEAN
Chapter10DelProtect2DoRequestOperationStatus(
    _In_ PFLT_CALLBACK_DATA Data
    );

EXTERN_C_END

//
//  Assign text sections for each routine.
//

#ifdef ALLOC_PRAGMA
#pragma alloc_text(INIT, DriverEntry)
#pragma alloc_text(PAGE, Chapter10DelProtect2Unload)
#pragma alloc_text(PAGE, Chapter10DelProtect2InstanceQueryTeardown)
#pragma alloc_text(PAGE, Chapter10DelProtect2InstanceSetup)
#pragma alloc_text(PAGE, Chapter10DelProtect2InstanceTeardownStart)
#pragma alloc_text(PAGE, Chapter10DelProtect2InstanceTeardownComplete)
#endif

//
//  operation registration
//

int findDir(PCUNICODE_STRING name, bool dosName)
{
	if (dirNamesCount == 0)
	{
		return -1;
	}
	for (int i = 0; i < MaxDirNums; i++)
	{
		auto& dir = dosName ? dirNames[i].dosName : dirNames[i].ntName;

        DbgPrintEx(77, 0, "dir = %wZ name = %wZ \n", &dir, name);
        
		//RtlEqualUnicodeString检查字符串的相等性，采用忽略大小写的方式
		if (dir.Buffer && RtlEqualUnicodeString(name, &dir, TRUE))
		{
			return i;
		}
	}
	return -1;
}

bool IsDeleteAllowed(PFLT_CALLBACK_DATA data)
{
    PFLT_FILE_NAME_INFORMATION info = nullptr;
    auto allow = true;

    do 
    {
        auto status = FltGetFileNameInformation(data, FLT_FILE_NAME_QUERY_DEFAULT | FLT_FILE_NAME_NORMALIZED, &info);
        if (!NT_SUCCESS(status))
        {
            break;
        }

        status = FltParseFileNameInformation(info);
		if (!NT_SUCCESS(status))
		{
			break;
		}

        UNICODE_STRING path;
        path.Length = path.MaximumLength = info->Volume.Length + info->Share.Length + info->ParentDir.Length;
        path.Buffer = info->Volume.Buffer;

        DbgPrintEx(77, 0, "path = %wZ \n", path);

        AutoLock<FastMutex> lock(dirNamesLock);
        if (findDir(&path, false) >= 0)
        {
            allow = false;
            DbgPrintEx(77, 0, "file not allowed to delete : %wZ \n", &info->Name);
        }

    } while (0);

    if (info)
    {
        FltReleaseFileNameInformation(info);
    }

	return allow;
}

FLT_PREOP_CALLBACK_STATUS FLTAPI delProtectPreCreate(
	_Inout_ PFLT_CALLBACK_DATA Data,
	_In_ PCFLT_RELATED_OBJECTS FltObjects,
	_Outptr_result_maybenull_ PVOID* CompletionContext
)
{
	UNREFERENCED_PARAMETER(FltObjects);
	UNREFERENCED_PARAMETER(CompletionContext);
	//使用 FLT_PREOP_COMPLETE 时，表示你彻底处理了请求，不希望其他后续操作被执行。
	//使用 FLT_PREOP_SUCCESS_NO_CALLBACK 时，表示请求成功，但你的驱动并不关注后面的处理，允许其他过滤器继续接管。

	if (Data->RequestorMode == KernelMode)
	{
		//判断请求，是否来自于内核
		return FLT_PREOP_SUCCESS_NO_CALLBACK;
	}

	auto& params = Data->Iopb->Parameters.Create;
	auto retStatus = FLT_PREOP_SUCCESS_NO_CALLBACK;

	if (params.Options & FILE_DELETE_ON_CLOSE)
	{
		//检查FILE_DELETE_ON_CLOSE标志是否存在于创建请求中

		DbgPrintEx(77, 0, "delete on close : %wZ \n", &Data->Iopb->TargetFileObject->FileName);

		if (!IsDeleteAllowed(Data))
		{

			Data->IoStatus.Status = STATUS_ACCESS_DENIED;
			retStatus = FLT_PREOP_COMPLETE;
		}
	}

	return retStatus;
}

FLT_PREOP_CALLBACK_STATUS FLTAPI delProtectPreSetInfo(
	_Inout_ PFLT_CALLBACK_DATA Data,
	_In_ PCFLT_RELATED_OBJECTS FltObjects,
	_Outptr_result_maybenull_ PVOID* CompletionContext
)
{
	UNREFERENCED_PARAMETER(FltObjects);
	UNREFERENCED_PARAMETER(CompletionContext);

	if (Data->RequestorMode == KernelMode)
	{
		//判断请求，是否来自于内核
		return FLT_PREOP_SUCCESS_NO_CALLBACK;
	}

	auto& params = Data->Iopb->Parameters.SetFileInformation;

	//FileInformationClass表示本实例代表的操作类型
	if (params.FileInformationClass != FileDispositionInformation &&
		params.FileInformationClass != FileDispositionInformationEx
		)
	{
		return FLT_PREOP_SUCCESS_NO_CALLBACK;
	}

	auto info = (FILE_DISPOSITION_INFORMATION*)params.InfoBuffer;
	if (!info->DeleteFile)
	{
		return FLT_PREOP_SUCCESS_NO_CALLBACK;
	}

	auto retStatus = FLT_PREOP_SUCCESS_NO_CALLBACK;

	if (!IsDeleteAllowed(Data))
	{
		Data->IoStatus.Status = STATUS_ACCESS_DENIED;
		retStatus = FLT_PREOP_COMPLETE;

		DbgPrintEx(77, 0, "prevent delete from IRP_MJ_SET_INFORMATION by %wZ \n", &Data->Iopb->TargetFileObject->FileName);
	}

	return retStatus;
}

CONST FLT_OPERATION_REGISTRATION Callbacks[] = {

	{IRP_MJ_CREATE, 0, delProtectPreCreate, nullptr}, //通FILE_DELETE_ON_CLOSE标志打开文件，所有句柄关闭此文件就会被删除
	{IRP_MJ_SET_INFORMATION, 0, delProtectPreSetInfo, nullptr}, //提供了一堆操作功能，删除只是其中一种

#if 0 // TODO - List all of the requests to filter.
    { IRP_MJ_CREATE,
      0,
      Chapter10DelProtect2PreOperation,
      Chapter10DelProtect2PostOperation },

    { IRP_MJ_CREATE_NAMED_PIPE,
      0,
      Chapter10DelProtect2PreOperation,
      Chapter10DelProtect2PostOperation },

    { IRP_MJ_CLOSE,
      0,
      Chapter10DelProtect2PreOperation,
      Chapter10DelProtect2PostOperation },

    { IRP_MJ_READ,
      0,
      Chapter10DelProtect2PreOperation,
      Chapter10DelProtect2PostOperation },

    { IRP_MJ_WRITE,
      0,
      Chapter10DelProtect2PreOperation,
      Chapter10DelProtect2PostOperation },

    { IRP_MJ_QUERY_INFORMATION,
      0,
      Chapter10DelProtect2PreOperation,
      Chapter10DelProtect2PostOperation },

    { IRP_MJ_SET_INFORMATION,
      0,
      Chapter10DelProtect2PreOperation,
      Chapter10DelProtect2PostOperation },

    { IRP_MJ_QUERY_EA,
      0,
      Chapter10DelProtect2PreOperation,
      Chapter10DelProtect2PostOperation },

    { IRP_MJ_SET_EA,
      0,
      Chapter10DelProtect2PreOperation,
      Chapter10DelProtect2PostOperation },

    { IRP_MJ_FLUSH_BUFFERS,
      0,
      Chapter10DelProtect2PreOperation,
      Chapter10DelProtect2PostOperation },

    { IRP_MJ_QUERY_VOLUME_INFORMATION,
      0,
      Chapter10DelProtect2PreOperation,
      Chapter10DelProtect2PostOperation },

    { IRP_MJ_SET_VOLUME_INFORMATION,
      0,
      Chapter10DelProtect2PreOperation,
      Chapter10DelProtect2PostOperation },

    { IRP_MJ_DIRECTORY_CONTROL,
      0,
      Chapter10DelProtect2PreOperation,
      Chapter10DelProtect2PostOperation },

    { IRP_MJ_FILE_SYSTEM_CONTROL,
      0,
      Chapter10DelProtect2PreOperation,
      Chapter10DelProtect2PostOperation },

    { IRP_MJ_DEVICE_CONTROL,
      0,
      Chapter10DelProtect2PreOperation,
      Chapter10DelProtect2PostOperation },

    { IRP_MJ_INTERNAL_DEVICE_CONTROL,
      0,
      Chapter10DelProtect2PreOperation,
      Chapter10DelProtect2PostOperation },

    { IRP_MJ_SHUTDOWN,
      0,
      Chapter10DelProtect2PreOperationNoPostOperation,
      NULL },                               //post operations not supported

    { IRP_MJ_LOCK_CONTROL,
      0,
      Chapter10DelProtect2PreOperation,
      Chapter10DelProtect2PostOperation },

    { IRP_MJ_CLEANUP,
      0,
      Chapter10DelProtect2PreOperation,
      Chapter10DelProtect2PostOperation },

    { IRP_MJ_CREATE_MAILSLOT,
      0,
      Chapter10DelProtect2PreOperation,
      Chapter10DelProtect2PostOperation },

    { IRP_MJ_QUERY_SECURITY,
      0,
      Chapter10DelProtect2PreOperation,
      Chapter10DelProtect2PostOperation },

    { IRP_MJ_SET_SECURITY,
      0,
      Chapter10DelProtect2PreOperation,
      Chapter10DelProtect2PostOperation },

    { IRP_MJ_QUERY_QUOTA,
      0,
      Chapter10DelProtect2PreOperation,
      Chapter10DelProtect2PostOperation },

    { IRP_MJ_SET_QUOTA,
      0,
      Chapter10DelProtect2PreOperation,
      Chapter10DelProtect2PostOperation },

    { IRP_MJ_PNP,
      0,
      Chapter10DelProtect2PreOperation,
      Chapter10DelProtect2PostOperation },

    { IRP_MJ_ACQUIRE_FOR_SECTION_SYNCHRONIZATION,
      0,
      Chapter10DelProtect2PreOperation,
      Chapter10DelProtect2PostOperation },

    { IRP_MJ_RELEASE_FOR_SECTION_SYNCHRONIZATION,
      0,
      Chapter10DelProtect2PreOperation,
      Chapter10DelProtect2PostOperation },

    { IRP_MJ_ACQUIRE_FOR_MOD_WRITE,
      0,
      Chapter10DelProtect2PreOperation,
      Chapter10DelProtect2PostOperation },

    { IRP_MJ_RELEASE_FOR_MOD_WRITE,
      0,
      Chapter10DelProtect2PreOperation,
      Chapter10DelProtect2PostOperation },

    { IRP_MJ_ACQUIRE_FOR_CC_FLUSH,
      0,
      Chapter10DelProtect2PreOperation,
      Chapter10DelProtect2PostOperation },

    { IRP_MJ_RELEASE_FOR_CC_FLUSH,
      0,
      Chapter10DelProtect2PreOperation,
      Chapter10DelProtect2PostOperation },

    { IRP_MJ_FAST_IO_CHECK_IF_POSSIBLE,
      0,
      Chapter10DelProtect2PreOperation,
      Chapter10DelProtect2PostOperation },

    { IRP_MJ_NETWORK_QUERY_OPEN,
      0,
      Chapter10DelProtect2PreOperation,
      Chapter10DelProtect2PostOperation },

    { IRP_MJ_MDL_READ,
      0,
      Chapter10DelProtect2PreOperation,
      Chapter10DelProtect2PostOperation },

    { IRP_MJ_MDL_READ_COMPLETE,
      0,
      Chapter10DelProtect2PreOperation,
      Chapter10DelProtect2PostOperation },

    { IRP_MJ_PREPARE_MDL_WRITE,
      0,
      Chapter10DelProtect2PreOperation,
      Chapter10DelProtect2PostOperation },

    { IRP_MJ_MDL_WRITE_COMPLETE,
      0,
      Chapter10DelProtect2PreOperation,
      Chapter10DelProtect2PostOperation },

    { IRP_MJ_VOLUME_MOUNT,
      0,
      Chapter10DelProtect2PreOperation,
      Chapter10DelProtect2PostOperation },

    { IRP_MJ_VOLUME_DISMOUNT,
      0,
      Chapter10DelProtect2PreOperation,
      Chapter10DelProtect2PostOperation },

#endif // TODO

    { IRP_MJ_OPERATION_END }
};

//
//  This defines what we want to filter with FltMgr
//

CONST FLT_REGISTRATION FilterRegistration = {

    sizeof( FLT_REGISTRATION ),         //  Size
    FLT_REGISTRATION_VERSION,           //  Version
    0,                                  //  Flags

    NULL,                               //  Context
    Callbacks,                          //  Operation callbacks

    Chapter10DelProtect2Unload,                           //  MiniFilterUnload

    Chapter10DelProtect2InstanceSetup,                    //  InstanceSetup
    Chapter10DelProtect2InstanceQueryTeardown,            //  InstanceQueryTeardown
    Chapter10DelProtect2InstanceTeardownStart,            //  InstanceTeardownStart
    Chapter10DelProtect2InstanceTeardownComplete,         //  InstanceTeardownComplete

    NULL,                               //  GenerateFileName
    NULL,                               //  GenerateDestinationFileName
    NULL                                //  NormalizeNameComponent

};



NTSTATUS
Chapter10DelProtect2InstanceSetup (
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_ FLT_INSTANCE_SETUP_FLAGS Flags,
    _In_ DEVICE_TYPE VolumeDeviceType,
    _In_ FLT_FILESYSTEM_TYPE VolumeFilesystemType
    )
/*++

Routine Description:

    This routine is called whenever a new instance is created on a volume. This
    gives us a chance to decide if we need to attach to this volume or not.

    If this routine is not defined in the registration structure, automatic
    instances are always created.

Arguments:

    FltObjects - Pointer to the FLT_RELATED_OBJECTS data structure containing
        opaque handles to this filter, instance and its associated volume.

    Flags - Flags describing the reason for this attach request.

Return Value:

    STATUS_SUCCESS - attach
    STATUS_FLT_DO_NOT_ATTACH - do not attach

--*/
{
    UNREFERENCED_PARAMETER( FltObjects );
    UNREFERENCED_PARAMETER( Flags );
    UNREFERENCED_PARAMETER( VolumeDeviceType );
    UNREFERENCED_PARAMETER( VolumeFilesystemType );

    PAGED_CODE();

    PT_DBG_PRINT( PTDBG_TRACE_ROUTINES,
                  ("Chapter10DelProtect2!Chapter10DelProtect2InstanceSetup: Entered\n") );

    return STATUS_SUCCESS;
}


NTSTATUS
Chapter10DelProtect2InstanceQueryTeardown (
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_ FLT_INSTANCE_QUERY_TEARDOWN_FLAGS Flags
    )
/*++

Routine Description:

    This is called when an instance is being manually deleted by a
    call to FltDetachVolume or FilterDetach thereby giving us a
    chance to fail that detach request.

    If this routine is not defined in the registration structure, explicit
    detach requests via FltDetachVolume or FilterDetach will always be
    failed.

Arguments:

    FltObjects - Pointer to the FLT_RELATED_OBJECTS data structure containing
        opaque handles to this filter, instance and its associated volume.

    Flags - Indicating where this detach request came from.

Return Value:

    Returns the status of this operation.

--*/
{
    UNREFERENCED_PARAMETER( FltObjects );
    UNREFERENCED_PARAMETER( Flags );

    PAGED_CODE();

    PT_DBG_PRINT( PTDBG_TRACE_ROUTINES,
                  ("Chapter10DelProtect2!Chapter10DelProtect2InstanceQueryTeardown: Entered\n") );

    return STATUS_SUCCESS;
}


VOID
Chapter10DelProtect2InstanceTeardownStart (
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_ FLT_INSTANCE_TEARDOWN_FLAGS Flags
    )
/*++

Routine Description:

    This routine is called at the start of instance teardown.

Arguments:

    FltObjects - Pointer to the FLT_RELATED_OBJECTS data structure containing
        opaque handles to this filter, instance and its associated volume.

    Flags - Reason why this instance is being deleted.

Return Value:

    None.

--*/
{
    UNREFERENCED_PARAMETER( FltObjects );
    UNREFERENCED_PARAMETER( Flags );

    PAGED_CODE();

    PT_DBG_PRINT( PTDBG_TRACE_ROUTINES,
                  ("Chapter10DelProtect2!Chapter10DelProtect2InstanceTeardownStart: Entered\n") );
}


VOID
Chapter10DelProtect2InstanceTeardownComplete (
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_ FLT_INSTANCE_TEARDOWN_FLAGS Flags
    )
/*++

Routine Description:

    This routine is called at the end of instance teardown.

Arguments:

    FltObjects - Pointer to the FLT_RELATED_OBJECTS data structure containing
        opaque handles to this filter, instance and its associated volume.

    Flags - Reason why this instance is being deleted.

Return Value:

    None.

--*/
{
    UNREFERENCED_PARAMETER( FltObjects );
    UNREFERENCED_PARAMETER( Flags );

    PAGED_CODE();

    PT_DBG_PRINT( PTDBG_TRACE_ROUTINES,
                  ("Chapter10DelProtect2!Chapter10DelProtect2InstanceTeardownComplete: Entered\n") );
}


/*************************************************************************
    MiniFilter initialization and unload routines.
*************************************************************************/


NTSTATUS convertDosNameToNtName(PCWSTR dosName, PUNICODE_STRING ntName)
{
    ntName->Buffer = nullptr;
    auto dosNameLen = wcslen(dosName);

    if (dosNameLen < 3)
    {
        return STATUS_BUFFER_TOO_SMALL;
    }

    if (dosName[2] != L'\\' || dosName[1] != L':')
    {
        return STATUS_INVALID_PARAMETER;
    }


    UNICODE_STRING link;
    WCHAR buf[20] = { 0 };
    RtlInitEmptyUnicodeString(&link, buf, sizeof(buf));

    DbgPrintEx(77, 0, "dos = %ws \n", dosName);

    wcsncpy(link.Buffer, L"\\??\\", 4);
    wcsncpy(link.Buffer + 4, dosName, 2);

    link.Length = 12;

    DbgPrintEx(77, 0, "link = %ws \n", link.Buffer);
    DbgPrintEx(77, 0, "link = %wZ \n", &link);

    OBJECT_ATTRIBUTES linkAttr;
    InitializeObjectAttributes(&linkAttr, &link, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, nullptr, nullptr);
    HANDLE hLink = nullptr;
    auto status = STATUS_SUCCESS;

    do 
    {
        status = ZwOpenSymbolicLinkObject(&hLink, GENERIC_READ, &linkAttr);
        if (!NT_SUCCESS(status))
        {
            break;
        }
        USHORT maxLen = 1024;
        ntName->Buffer = (WCHAR*)ExAllocatePoolWithTag(PagedPool, maxLen, 0);
        if (!ntName->Buffer)
        {
            status = STATUS_INSUFFICIENT_RESOURCES;
            break;
        }
        ntName->MaximumLength = maxLen;

        status = ZwQuerySymbolicLinkObject(hLink, ntName, nullptr);
        if (!NT_SUCCESS(status))
        {
            break;
        }
    } while (0);

    if (!NT_SUCCESS(status))
    {
        if (ntName->Buffer)
        {
            ExFreePoolWithTag(ntName->Buffer, 0);
            ntName->Buffer = nullptr;
        }
    }
    else
    {
        RtlAppendUnicodeToString(ntName, dosName + 2);
    }

    if (hLink)
    {
        ZwClose(hLink);
    }

    return status;
}

void clearAll() 
{
    AutoLock<FastMutex> lock(dirNamesLock);
	for (int i = 0; i < MaxDirNums; i++) 
    {
		if (dirNames[i].dosName.Buffer)
        {
			ExFreePoolWithTag(dirNames[i].dosName.Buffer, 0);
            dirNames[i].dosName.Buffer = nullptr;
		}
		if (dirNames[i].ntName.Buffer) 
        {
            ExFreePoolWithTag(dirNames[i].ntName.Buffer, 0);
            dirNames[i].ntName.Buffer = nullptr;
		}
	}
	dirNamesCount = 0;
}

VOID DriverUnload(
	_In_ struct _DRIVER_OBJECT* DriverObject
)
{
	clearAll();

	UNICODE_STRING linkName = RTL_CONSTANT_STRING(L"\\??\\delprotect2");

	IoDeleteSymbolicLink(&linkName);

	if (DriverObject->DeviceObject)
	{
		IoDeleteDevice(DriverObject->DeviceObject);
	}
}

NTSTATUS createClose(
	_In_ struct _DEVICE_OBJECT* DeviceObject,
	_Inout_ struct _IRP* Irp
)
{
	UNREFERENCED_PARAMETER(DeviceObject);
	Irp->IoStatus.Status = STATUS_SUCCESS;
	Irp->IoStatus.Information = 0;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	return STATUS_SUCCESS;
}

NTSTATUS ctl(
    _In_ struct _DEVICE_OBJECT* DeviceObject,
    _Inout_ struct _IRP* Irp
)
{
	UNREFERENCED_PARAMETER(DeviceObject);
	auto stack = IoGetCurrentIrpStackLocation(Irp);
	auto status = STATUS_SUCCESS;
	PVOID buf = Irp->AssociatedIrp.SystemBuffer;
    auto bufLen = stack->Parameters.DeviceIoControl.InputBufferLength;

	switch (stack->Parameters.DeviceIoControl.IoControlCode)
	{
	case ADD_DIR:
	{
		auto name = (WCHAR*)buf;
		if (!name)
		{
			status = STATUS_INVALID_PARAMETER;
			break;
		}
        if (bufLen > 1024)
        {
            status = STATUS_INVALID_PARAMETER;
            break;
        }
        name[bufLen / sizeof(WCHAR) - 1] = L'\0';

        auto dosNameLen = wcslen(name);
        if (dosNameLen < 3)
        {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }

        AutoLock<FastMutex> lock(dirNamesLock);

        UNICODE_STRING strName;
        RtlInitUnicodeString(&strName, name);

        if (findDir(&strName, true) >= 0)
        {
            break;
        }

        if (dirNamesCount == MaxDirNums)
        {
            status = STATUS_TOO_MANY_NAMES;
            break;
        }

        for (int i = 0; i < MaxDirNums; i++)
        {
            if (dirNames[i].dosName.Buffer == nullptr)
            {
                auto len = (dosNameLen + 2) * sizeof(WCHAR);
                auto tmp = (WCHAR*)ExAllocatePoolWithTag(PagedPool, len, 0);
                if (!tmp)
                {
                    status = STATUS_INSUFFICIENT_RESOURCES;
                    break;
                }
                wcscpy_s(tmp, len / sizeof(WCHAR), name);

                if (name[dosNameLen - 1] != L'\\')
                {
                    wcscat_s(tmp, dosNameLen + 2, L"\\");
                }

                status = convertDosNameToNtName(tmp, &dirNames[i].ntName);
                if (!NT_SUCCESS(status))
                {
                    ExFreePoolWithTag(tmp, 0);
                    break;
                }

                RtlInitUnicodeString(&dirNames[i].dosName, tmp);

                DbgPrintEx(77, 0, "add %wZ <==> %wZ \n", &dirNames[i].dosName, &dirNames[i].ntName);

                ++dirNamesCount;
                break;
            }
        }

		break;
	}
	case DEL_DIR:
	{
		auto name = (WCHAR*)buf;
		if (!name)
		{
			status = STATUS_INVALID_PARAMETER;
			break;
		}
		if (bufLen > 1024)
		{
			status = STATUS_INVALID_PARAMETER;
			break;
		}
		name[bufLen / sizeof(WCHAR) - 1] = L'\0';

		auto dosNameLen = wcslen(name);
		if (dosNameLen < 3)
		{
			status = STATUS_BUFFER_TOO_SMALL;
			break;
		}

		AutoLock<FastMutex> lock(dirNamesLock);

		UNICODE_STRING strName;
		RtlInitUnicodeString(&strName, name);

		int found = findDir(&strName, true);
		if (found >= 0) 
        {
			dirNames[found].Free();
			dirNamesCount--;
		}
		else 
        {
			status = STATUS_NOT_FOUND;
		}

		break;
	}
	case CLEAR_DIR:
	{
		clearAll();

		break;
	}
	}

	Irp->IoStatus.Status = status;
	Irp->IoStatus.Information = 0;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	return status;
}

NTSTATUS
DriverEntry (
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath
    )
/*++

Routine Description:

    This is the initialization routine for this miniFilter driver.  This
    registers with FltMgr and initializes all global data structures.

Arguments:

    DriverObject - Pointer to driver object created by the system to
        represent this driver.

    RegistryPath - Unicode string identifying where the parameters for this
        driver are located in the registry.

Return Value:

    Routine can return non success error codes.

--*/
{
    NTSTATUS status;

    UNREFERENCED_PARAMETER( RegistryPath );

    DbgPrintEx(77, 0, "%wZ \n", RegistryPath);

    PT_DBG_PRINT( PTDBG_TRACE_ROUTINES,
                  ("Chapter10DelProtect2!DriverEntry: Entered\n") );

	PDEVICE_OBJECT devObj = NULL;
	UNICODE_STRING devName = RTL_CONSTANT_STRING(L"\\Device\\delprotect2");
	UNICODE_STRING linkName = RTL_CONSTANT_STRING(L"\\??\\delprotect2");
	auto linkCreate = false;

	do
	{
		status = IoCreateDevice(DriverObject, 0, &devName, FILE_DEVICE_UNKNOWN, 0, false, &devObj);
		if (!NT_SUCCESS(status))
		{
			break;
		}
		devObj->Flags |= DO_BUFFERED_IO;

		status = IoCreateSymbolicLink(&linkName, &devName);
		if (!NT_SUCCESS(status))
		{
			break;
		}
		linkCreate = true;

		//
		//  Register with FltMgr to tell it our callback routines
		//
		status = FltRegisterFilter(DriverObject, &FilterRegistration, &gFilterHandle);

		FLT_ASSERT(NT_SUCCESS(status));

		if (!NT_SUCCESS(status))
		{
			break;
		}

		DriverObject->DriverUnload = DriverUnload;
		DriverObject->MajorFunction[IRP_MJ_CREATE] = createClose;
		DriverObject->MajorFunction[IRP_MJ_CLOSE] = createClose;
		DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = ctl;

		dirNamesLock.Init();

		status = FltStartFiltering(gFilterHandle);
	} while (0);

	if (!NT_SUCCESS(status)) {
		if (gFilterHandle)
		{
			FltUnregisterFilter(gFilterHandle);
		}
		if (linkCreate)
		{
			IoDeleteSymbolicLink(&linkName);
		}
		if (devObj)
		{
			IoDeleteDevice(devObj);
		}
	}

    return status;
}

NTSTATUS
Chapter10DelProtect2Unload (
    _In_ FLT_FILTER_UNLOAD_FLAGS Flags
    )
/*++

Routine Description:

    This is the unload routine for this miniFilter driver. This is called
    when the minifilter is about to be unloaded. We can fail this unload
    request if this is not a mandatory unload indicated by the Flags
    parameter.

Arguments:

    Flags - Indicating if this is a mandatory unload.

Return Value:

    Returns STATUS_SUCCESS.

--*/
{
    UNREFERENCED_PARAMETER( Flags );

    PAGED_CODE();

    PT_DBG_PRINT( PTDBG_TRACE_ROUTINES,
                  ("Chapter10DelProtect2!Chapter10DelProtect2Unload: Entered\n") );

    FltUnregisterFilter( gFilterHandle );

    return STATUS_SUCCESS;
}


/*************************************************************************
    MiniFilter callback routines.
*************************************************************************/
FLT_PREOP_CALLBACK_STATUS
Chapter10DelProtect2PreOperation (
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _Flt_CompletionContext_Outptr_ PVOID *CompletionContext
    )
/*++

Routine Description:

    This routine is a pre-operation dispatch routine for this miniFilter.

    This is non-pageable because it could be called on the paging path

Arguments:

    Data - Pointer to the filter callbackData that is passed to us.

    FltObjects - Pointer to the FLT_RELATED_OBJECTS data structure containing
        opaque handles to this filter, instance, its associated volume and
        file object.

    CompletionContext - The context for the completion routine for this
        operation.

Return Value:

    The return value is the status of the operation.

--*/
{
    NTSTATUS status;

    UNREFERENCED_PARAMETER( FltObjects );
    UNREFERENCED_PARAMETER( CompletionContext );

    PT_DBG_PRINT( PTDBG_TRACE_ROUTINES,
                  ("Chapter10DelProtect2!Chapter10DelProtect2PreOperation: Entered\n") );

    //
    //  See if this is an operation we would like the operation status
    //  for.  If so request it.
    //
    //  NOTE: most filters do NOT need to do this.  You only need to make
    //        this call if, for example, you need to know if the oplock was
    //        actually granted.
    //

    if (Chapter10DelProtect2DoRequestOperationStatus( Data )) {

        status = FltRequestOperationStatusCallback( Data,
                                                    Chapter10DelProtect2OperationStatusCallback,
                                                    (PVOID)(++OperationStatusCtx) );
        if (!NT_SUCCESS(status)) {

            PT_DBG_PRINT( PTDBG_TRACE_OPERATION_STATUS,
                          ("Chapter10DelProtect2!Chapter10DelProtect2PreOperation: FltRequestOperationStatusCallback Failed, status=%08x\n",
                           status) );
        }
    }

    // This template code does not do anything with the callbackData, but
    // rather returns FLT_PREOP_SUCCESS_WITH_CALLBACK.
    // This passes the request down to the next miniFilter in the chain.

    return FLT_PREOP_SUCCESS_WITH_CALLBACK;
}



VOID
Chapter10DelProtect2OperationStatusCallback (
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_ PFLT_IO_PARAMETER_BLOCK ParameterSnapshot,
    _In_ NTSTATUS OperationStatus,
    _In_ PVOID RequesterContext
    )
/*++

Routine Description:

    This routine is called when the given operation returns from the call
    to IoCallDriver.  This is useful for operations where STATUS_PENDING
    means the operation was successfully queued.  This is useful for OpLocks
    and directory change notification operations.

    This callback is called in the context of the originating thread and will
    never be called at DPC level.  The file object has been correctly
    referenced so that you can access it.  It will be automatically
    dereferenced upon return.

    This is non-pageable because it could be called on the paging path

Arguments:

    FltObjects - Pointer to the FLT_RELATED_OBJECTS data structure containing
        opaque handles to this filter, instance, its associated volume and
        file object.

    RequesterContext - The context for the completion routine for this
        operation.

    OperationStatus -

Return Value:

    The return value is the status of the operation.

--*/
{
    UNREFERENCED_PARAMETER( FltObjects );

    PT_DBG_PRINT( PTDBG_TRACE_ROUTINES,
                  ("Chapter10DelProtect2!Chapter10DelProtect2OperationStatusCallback: Entered\n") );

    PT_DBG_PRINT( PTDBG_TRACE_OPERATION_STATUS,
                  ("Chapter10DelProtect2!Chapter10DelProtect2OperationStatusCallback: Status=%08x ctx=%p IrpMj=%02x.%02x \"%s\"\n",
                   OperationStatus,
                   RequesterContext,
                   ParameterSnapshot->MajorFunction,
                   ParameterSnapshot->MinorFunction,
                   FltGetIrpName(ParameterSnapshot->MajorFunction)) );
}


FLT_POSTOP_CALLBACK_STATUS
Chapter10DelProtect2PostOperation (
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_opt_ PVOID CompletionContext,
    _In_ FLT_POST_OPERATION_FLAGS Flags
    )
/*++

Routine Description:

    This routine is the post-operation completion routine for this
    miniFilter.

    This is non-pageable because it may be called at DPC level.

Arguments:

    Data - Pointer to the filter callbackData that is passed to us.

    FltObjects - Pointer to the FLT_RELATED_OBJECTS data structure containing
        opaque handles to this filter, instance, its associated volume and
        file object.

    CompletionContext - The completion context set in the pre-operation routine.

    Flags - Denotes whether the completion is successful or is being drained.

Return Value:

    The return value is the status of the operation.

--*/
{
    UNREFERENCED_PARAMETER( Data );
    UNREFERENCED_PARAMETER( FltObjects );
    UNREFERENCED_PARAMETER( CompletionContext );
    UNREFERENCED_PARAMETER( Flags );

    PT_DBG_PRINT( PTDBG_TRACE_ROUTINES,
                  ("Chapter10DelProtect2!Chapter10DelProtect2PostOperation: Entered\n") );

    return FLT_POSTOP_FINISHED_PROCESSING;
}


FLT_PREOP_CALLBACK_STATUS
Chapter10DelProtect2PreOperationNoPostOperation (
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _Flt_CompletionContext_Outptr_ PVOID *CompletionContext
    )
/*++

Routine Description:

    This routine is a pre-operation dispatch routine for this miniFilter.

    This is non-pageable because it could be called on the paging path

Arguments:

    Data - Pointer to the filter callbackData that is passed to us.

    FltObjects - Pointer to the FLT_RELATED_OBJECTS data structure containing
        opaque handles to this filter, instance, its associated volume and
        file object.

    CompletionContext - The context for the completion routine for this
        operation.

Return Value:

    The return value is the status of the operation.

--*/
{
    UNREFERENCED_PARAMETER( Data );
    UNREFERENCED_PARAMETER( FltObjects );
    UNREFERENCED_PARAMETER( CompletionContext );

    PT_DBG_PRINT( PTDBG_TRACE_ROUTINES,
                  ("Chapter10DelProtect2!Chapter10DelProtect2PreOperationNoPostOperation: Entered\n") );

    // This template code does not do anything with the callbackData, but
    // rather returns FLT_PREOP_SUCCESS_NO_CALLBACK.
    // This passes the request down to the next miniFilter in the chain.

    return FLT_PREOP_SUCCESS_NO_CALLBACK;
}


BOOLEAN
Chapter10DelProtect2DoRequestOperationStatus(
    _In_ PFLT_CALLBACK_DATA Data
    )
/*++

Routine Description:

    This identifies those operations we want the operation status for.  These
    are typically operations that return STATUS_PENDING as a normal completion
    status.

Arguments:

Return Value:

    TRUE - If we want the operation status
    FALSE - If we don't

--*/
{
    PFLT_IO_PARAMETER_BLOCK iopb = Data->Iopb;

    //
    //  return boolean state based on which operations we are interested in
    //

    return (BOOLEAN)

            //
            //  Check for oplock operations
            //

             (((iopb->MajorFunction == IRP_MJ_FILE_SYSTEM_CONTROL) &&
               ((iopb->Parameters.FileSystemControl.Common.FsControlCode == FSCTL_REQUEST_FILTER_OPLOCK)  ||
                (iopb->Parameters.FileSystemControl.Common.FsControlCode == FSCTL_REQUEST_BATCH_OPLOCK)   ||
                (iopb->Parameters.FileSystemControl.Common.FsControlCode == FSCTL_REQUEST_OPLOCK_LEVEL_1) ||
                (iopb->Parameters.FileSystemControl.Common.FsControlCode == FSCTL_REQUEST_OPLOCK_LEVEL_2)))

              ||

              //
              //    Check for directy change notification
              //

              ((iopb->MajorFunction == IRP_MJ_DIRECTORY_CONTROL) &&
               (iopb->MinorFunction == IRP_MN_NOTIFY_CHANGE_DIRECTORY))
             );
}
