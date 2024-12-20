/*++

Module Name:

    Chapter10DelProtect.c

Abstract:

    This is the main module of the Chapter10_DelProtect miniFilter driver.

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

#define DRIVER_TAG 'dp'

//保存执行文件名
const int MaxExeNums = 32;
WCHAR* exeNames[MaxExeNums];
int exeNamesCount = 0;
FastMutex exeNamesLock;

#define PT_DBG_PRINT( _dbgLevel, _string )          \
    (FlagOn(gTraceFlags,(_dbgLevel)) ?              \
        DbgPrint _string :                          \
        ((int)0))

/*************************************************************************
    Prototypes
*************************************************************************/

extern "C" NTSTATUS NTAPI ZwQueryInformationProcess(
	__in HANDLE ProcessHandle,
	__in PROCESSINFOCLASS ProcessInformationClass,
	__out_bcount(ProcessInformationLength) PVOID ProcessInformation,
	__in ULONG ProcessInformationLength,
	__out_opt PULONG ReturnLength
);

EXTERN_C_START

DRIVER_INITIALIZE DriverEntry;
NTSTATUS
DriverEntry (
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath
    );

NTSTATUS
Chapter10DelProtectInstanceSetup (
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_ FLT_INSTANCE_SETUP_FLAGS Flags,
    _In_ DEVICE_TYPE VolumeDeviceType,
    _In_ FLT_FILESYSTEM_TYPE VolumeFilesystemType
    );

VOID
Chapter10DelProtectInstanceTeardownStart (
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_ FLT_INSTANCE_TEARDOWN_FLAGS Flags
    );

VOID
Chapter10DelProtectInstanceTeardownComplete (
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_ FLT_INSTANCE_TEARDOWN_FLAGS Flags
    );

NTSTATUS
Chapter10DelProtectUnload (
    _In_ FLT_FILTER_UNLOAD_FLAGS Flags
    );

NTSTATUS
Chapter10DelProtectInstanceQueryTeardown (
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_ FLT_INSTANCE_QUERY_TEARDOWN_FLAGS Flags
    );

FLT_PREOP_CALLBACK_STATUS
Chapter10DelProtectPreOperation (
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _Flt_CompletionContext_Outptr_ PVOID *CompletionContext
    );

VOID
Chapter10DelProtectOperationStatusCallback (
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_ PFLT_IO_PARAMETER_BLOCK ParameterSnapshot,
    _In_ NTSTATUS OperationStatus,
    _In_ PVOID RequesterContext
    );

FLT_POSTOP_CALLBACK_STATUS
Chapter10DelProtectPostOperation (
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_opt_ PVOID CompletionContext,
    _In_ FLT_POST_OPERATION_FLAGS Flags
    );

FLT_PREOP_CALLBACK_STATUS
Chapter10DelProtectPreOperationNoPostOperation (
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _Flt_CompletionContext_Outptr_ PVOID *CompletionContext
    );

BOOLEAN
Chapter10DelProtectDoRequestOperationStatus(
    _In_ PFLT_CALLBACK_DATA Data
    );

EXTERN_C_END

//
//  Assign text sections for each routine.
//

#ifdef ALLOC_PRAGMA
#pragma alloc_text(INIT, DriverEntry)
#pragma alloc_text(PAGE, Chapter10DelProtectUnload)
#pragma alloc_text(PAGE, Chapter10DelProtectInstanceQueryTeardown)
#pragma alloc_text(PAGE, Chapter10DelProtectInstanceSetup)
#pragma alloc_text(PAGE, Chapter10DelProtectInstanceTeardownStart)
#pragma alloc_text(PAGE, Chapter10DelProtectInstanceTeardownComplete)
#endif

//
//  operation registration
//

bool findExe(PCWSTR name)
{
	AutoLock<FastMutex> lock(exeNamesLock);
	if (exeNamesCount == 0)
	{
		return false;
	}
	for (int i = 0; i < MaxExeNums; i++)
	{
		if (exeNames[i] && _wcsicmp(exeNames[i], name) == 0)
		{
			return true;
		}
	}
	return false;
}

void clearAll()
{
	AutoLock<FastMutex> lock(exeNamesLock);
	for (int i = 0; i < MaxExeNums; i++)
	{
		if (exeNames[i])
		{
			ExFreePoolWithTag(exeNames[i], DRIVER_TAG);
			exeNames[i] = nullptr;
		}
	}
	exeNamesCount = 0;
}

bool IsDeleteAllowed(PEPROCESS process)
{
    bool curProcess = PsGetCurrentProcess() == process;
    HANDLE hProcess;
    if (curProcess)
    {
        hProcess = NtCurrentProcess();
    }
    else
    {
        auto status = ObOpenObjectByPointer(process, OBJ_KERNEL_HANDLE, nullptr, 0, nullptr, KernelMode, &hProcess);
        if (!NT_SUCCESS(status))
        {
            return true;
        }
    }
    auto size = 512;
    bool allowDel = true;
    auto processName = (UNICODE_STRING*)ExAllocatePoolWithTag(PagedPool, size, 0);

    if (processName)
    {
        RtlZeroMemory(processName, size);

        auto status = ZwQueryInformationProcess(NtCurrentProcess(), ProcessImageFileName, processName, size - sizeof(WCHAR), nullptr);

        if (NT_SUCCESS(status))
        {
            DbgPrintEx(77, 0, "delete operation from %wZ \n", processName);

            auto exeName = wcsrchr(processName->Buffer, L'\\');

			if (exeName && findExe(exeName + 1))
			{
                allowDel = false;
			}
        }

        ExFreePoolWithTag(processName, 0);
    }

    if (!curProcess)
    {
        ZwClose(hProcess);
    }

    return allowDel;
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

        if (!IsDeleteAllowed(PsGetCurrentProcess()))
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

	//我们必须检查最初的调用者提供的数据中的Thread字段。从这个线程，我们可以找到指向进程的指针
    auto process = PsGetThreadProcess(Data->Thread);

    auto retStatus = FLT_PREOP_SUCCESS_NO_CALLBACK;

	if (!IsDeleteAllowed(process))
	{
		Data->IoStatus.Status = STATUS_ACCESS_DENIED;
		retStatus = FLT_PREOP_COMPLETE;

        DbgPrintEx(77, 0, "prevent delete from IRP_MJ_SET_INFORMATION by cmd.exe \n");
	}

    return retStatus;
}

CONST FLT_OPERATION_REGISTRATION Callbacks[] = {

    {IRP_MJ_CREATE, 0, delProtectPreCreate, nullptr}, //通FILE_DELETE_ON_CLOSE标志打开文件，所有句柄关闭此文件就会被删除
	{IRP_MJ_SET_INFORMATION, 0, delProtectPreSetInfo, nullptr}, //提供了一堆操作功能，删除只是其中一种

#if 0 // TODO - List all of the requests to filter.
    { IRP_MJ_CREATE,
      0,
      Chapter10DelProtectPreOperation,
      Chapter10DelProtectPostOperation },

    { IRP_MJ_CREATE_NAMED_PIPE,
      0,
      Chapter10DelProtectPreOperation,
      Chapter10DelProtectPostOperation },

    { IRP_MJ_CLOSE,
      0,
      Chapter10DelProtectPreOperation,
      Chapter10DelProtectPostOperation },

    { IRP_MJ_READ,
      0,
      Chapter10DelProtectPreOperation,
      Chapter10DelProtectPostOperation },

    { IRP_MJ_WRITE,
      0,
      Chapter10DelProtectPreOperation,
      Chapter10DelProtectPostOperation },

    { IRP_MJ_QUERY_INFORMATION,
      0,
      Chapter10DelProtectPreOperation,
      Chapter10DelProtectPostOperation },

    { IRP_MJ_SET_INFORMATION,
      0,
      Chapter10DelProtectPreOperation,
      Chapter10DelProtectPostOperation },

    { IRP_MJ_QUERY_EA,
      0,
      Chapter10DelProtectPreOperation,
      Chapter10DelProtectPostOperation },

    { IRP_MJ_SET_EA,
      0,
      Chapter10DelProtectPreOperation,
      Chapter10DelProtectPostOperation },

    { IRP_MJ_FLUSH_BUFFERS,
      0,
      Chapter10DelProtectPreOperation,
      Chapter10DelProtectPostOperation },

    { IRP_MJ_QUERY_VOLUME_INFORMATION,
      0,
      Chapter10DelProtectPreOperation,
      Chapter10DelProtectPostOperation },

    { IRP_MJ_SET_VOLUME_INFORMATION,
      0,
      Chapter10DelProtectPreOperation,
      Chapter10DelProtectPostOperation },

    { IRP_MJ_DIRECTORY_CONTROL,
      0,
      Chapter10DelProtectPreOperation,
      Chapter10DelProtectPostOperation },

    { IRP_MJ_FILE_SYSTEM_CONTROL,
      0,
      Chapter10DelProtectPreOperation,
      Chapter10DelProtectPostOperation },

    { IRP_MJ_DEVICE_CONTROL,
      0,
      Chapter10DelProtectPreOperation,
      Chapter10DelProtectPostOperation },

    { IRP_MJ_INTERNAL_DEVICE_CONTROL,
      0,
      Chapter10DelProtectPreOperation,
      Chapter10DelProtectPostOperation },

    { IRP_MJ_SHUTDOWN,
      0,
      Chapter10DelProtectPreOperationNoPostOperation,
      NULL },                               //post operations not supported

    { IRP_MJ_LOCK_CONTROL,
      0,
      Chapter10DelProtectPreOperation,
      Chapter10DelProtectPostOperation },

    { IRP_MJ_CLEANUP,
      0,
      Chapter10DelProtectPreOperation,
      Chapter10DelProtectPostOperation },

    { IRP_MJ_CREATE_MAILSLOT,
      0,
      Chapter10DelProtectPreOperation,
      Chapter10DelProtectPostOperation },

    { IRP_MJ_QUERY_SECURITY,
      0,
      Chapter10DelProtectPreOperation,
      Chapter10DelProtectPostOperation },

    { IRP_MJ_SET_SECURITY,
      0,
      Chapter10DelProtectPreOperation,
      Chapter10DelProtectPostOperation },

    { IRP_MJ_QUERY_QUOTA,
      0,
      Chapter10DelProtectPreOperation,
      Chapter10DelProtectPostOperation },

    { IRP_MJ_SET_QUOTA,
      0,
      Chapter10DelProtectPreOperation,
      Chapter10DelProtectPostOperation },

    { IRP_MJ_PNP,
      0,
      Chapter10DelProtectPreOperation,
      Chapter10DelProtectPostOperation },

    { IRP_MJ_ACQUIRE_FOR_SECTION_SYNCHRONIZATION,
      0,
      Chapter10DelProtectPreOperation,
      Chapter10DelProtectPostOperation },

    { IRP_MJ_RELEASE_FOR_SECTION_SYNCHRONIZATION,
      0,
      Chapter10DelProtectPreOperation,
      Chapter10DelProtectPostOperation },

    { IRP_MJ_ACQUIRE_FOR_MOD_WRITE,
      0,
      Chapter10DelProtectPreOperation,
      Chapter10DelProtectPostOperation },

    { IRP_MJ_RELEASE_FOR_MOD_WRITE,
      0,
      Chapter10DelProtectPreOperation,
      Chapter10DelProtectPostOperation },

    { IRP_MJ_ACQUIRE_FOR_CC_FLUSH,
      0,
      Chapter10DelProtectPreOperation,
      Chapter10DelProtectPostOperation },

    { IRP_MJ_RELEASE_FOR_CC_FLUSH,
      0,
      Chapter10DelProtectPreOperation,
      Chapter10DelProtectPostOperation },

    { IRP_MJ_FAST_IO_CHECK_IF_POSSIBLE,
      0,
      Chapter10DelProtectPreOperation,
      Chapter10DelProtectPostOperation },

    { IRP_MJ_NETWORK_QUERY_OPEN,
      0,
      Chapter10DelProtectPreOperation,
      Chapter10DelProtectPostOperation },

    { IRP_MJ_MDL_READ,
      0,
      Chapter10DelProtectPreOperation,
      Chapter10DelProtectPostOperation },

    { IRP_MJ_MDL_READ_COMPLETE,
      0,
      Chapter10DelProtectPreOperation,
      Chapter10DelProtectPostOperation },

    { IRP_MJ_PREPARE_MDL_WRITE,
      0,
      Chapter10DelProtectPreOperation,
      Chapter10DelProtectPostOperation },

    { IRP_MJ_MDL_WRITE_COMPLETE,
      0,
      Chapter10DelProtectPreOperation,
      Chapter10DelProtectPostOperation },

    { IRP_MJ_VOLUME_MOUNT,
      0,
      Chapter10DelProtectPreOperation,
      Chapter10DelProtectPostOperation },

    { IRP_MJ_VOLUME_DISMOUNT,
      0,
      Chapter10DelProtectPreOperation,
      Chapter10DelProtectPostOperation },

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

    Chapter10DelProtectUnload,                           //  MiniFilterUnload

    Chapter10DelProtectInstanceSetup,                    //  InstanceSetup
    Chapter10DelProtectInstanceQueryTeardown,            //  InstanceQueryTeardown
    Chapter10DelProtectInstanceTeardownStart,            //  InstanceTeardownStart
    Chapter10DelProtectInstanceTeardownComplete,         //  InstanceTeardownComplete

    NULL,                               //  GenerateFileName
    NULL,                               //  GenerateDestinationFileName
    NULL                                //  NormalizeNameComponent

};



NTSTATUS
Chapter10DelProtectInstanceSetup (
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
                  ("Chapter10DelProtect!Chapter10DelProtectInstanceSetup: Entered\n") );

    return STATUS_SUCCESS;
}


NTSTATUS
Chapter10DelProtectInstanceQueryTeardown (
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
                  ("Chapter10DelProtect!Chapter10DelProtectInstanceQueryTeardown: Entered\n") );

    return STATUS_SUCCESS;
}


VOID
Chapter10DelProtectInstanceTeardownStart (
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
                  ("Chapter10DelProtect!Chapter10DelProtectInstanceTeardownStart: Entered\n") );
}


VOID
Chapter10DelProtectInstanceTeardownComplete (
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
                  ("Chapter10DelProtect!Chapter10DelProtectInstanceTeardownComplete: Entered\n") );
}


/*************************************************************************
    MiniFilter initialization and unload routines.
*************************************************************************/
VOID DriverUnload(
    _In_ struct _DRIVER_OBJECT* DriverObject
)
{
    clearAll();

    UNICODE_STRING linkName = RTL_CONSTANT_STRING(L"\\??\\delprotect");

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

    switch (stack->Parameters.DeviceIoControl.IoControlCode)
    {
    case ADD_EXE:
    {
        auto name = (WCHAR*)buf;
        if (!name)
        {
            status = STATUS_INVALID_PARAMETER;
            break;
        }

        if (findExe(name))
        {
            break;
        }

        AutoLock<FastMutex> lock(exeNamesLock);
        if (exeNamesCount == MaxExeNums)
        {
            status = STATUS_TOO_MANY_NAMES;
            break;
        }

        for (int i = 0; i < MaxExeNums; i++)
        {
            if (exeNames[i] == nullptr)
            {
                auto len = (wcslen(name) + 1) * sizeof(WCHAR);
                auto tmp = (WCHAR*)ExAllocatePoolWithTag(PagedPool, len, DRIVER_TAG);
                if (!tmp)
                {
                    status = STATUS_INSUFFICIENT_RESOURCES;
                    break;
                }
                wcscpy_s(tmp, len / sizeof(WCHAR), name);
                exeNames[i] = tmp;
                ++exeNamesCount;

                DbgPrintEx(77, 0, "添加 %ws \n", name);

                break;
            }
        }

        break;
    }
    case DEL_EXE:
    {
		auto name = (WCHAR*)buf;
		if (!name)
		{
			status = STATUS_INVALID_PARAMETER;
			break;
		}

        AutoLock<FastMutex> lock(exeNamesLock);
        auto find = false;
        for (int i = 0; i < MaxExeNums; i++)
        {
            if (_wcsicmp(exeNames[i], name) == 0)
            {
                ExFreePoolWithTag(exeNames[i], DRIVER_TAG);
                exeNames[i] = nullptr;
                --exeNamesCount;
                find = true;

                DbgPrintEx(77, 0, "删除 %ws \n", name);

                break;
            }
        }
        if (!find)
        {
            status = STATUS_NOT_FOUND;
        }

        break;
    }
    case CLEAR_EXE:
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

    //右键 .inf 文件安装驱动
    //fltmc load Chapter10_DelProtect
    //fltmc unload Chapter10_DelProtect

    DbgPrintEx(77, 0, "%wZ \n", RegistryPath);

    PT_DBG_PRINT( PTDBG_TRACE_ROUTINES,
                  ("Chapter10DelProtect!DriverEntry: Entered\n") );


    PDEVICE_OBJECT devObj = NULL;
    UNICODE_STRING devName = RTL_CONSTANT_STRING(L"\\Device\\delprotect");
    UNICODE_STRING linkName = RTL_CONSTANT_STRING(L"\\??\\delprotect");
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

        exeNamesLock.Init();

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
Chapter10DelProtectUnload (
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
                  ("Chapter10DelProtect!Chapter10DelProtectUnload: Entered\n") );

    FltUnregisterFilter( gFilterHandle );

    return STATUS_SUCCESS;
}


/*************************************************************************
    MiniFilter callback routines.
*************************************************************************/
FLT_PREOP_CALLBACK_STATUS
Chapter10DelProtectPreOperation (
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
                  ("Chapter10DelProtect!Chapter10DelProtectPreOperation: Entered\n") );

    //
    //  See if this is an operation we would like the operation status
    //  for.  If so request it.
    //
    //  NOTE: most filters do NOT need to do this.  You only need to make
    //        this call if, for example, you need to know if the oplock was
    //        actually granted.
    //

    if (Chapter10DelProtectDoRequestOperationStatus( Data )) {

        status = FltRequestOperationStatusCallback( Data,
                                                    Chapter10DelProtectOperationStatusCallback,
                                                    (PVOID)(++OperationStatusCtx) );
        if (!NT_SUCCESS(status)) {

            PT_DBG_PRINT( PTDBG_TRACE_OPERATION_STATUS,
                          ("Chapter10DelProtect!Chapter10DelProtectPreOperation: FltRequestOperationStatusCallback Failed, status=%08x\n",
                           status) );
        }
    }

    // This template code does not do anything with the callbackData, but
    // rather returns FLT_PREOP_SUCCESS_WITH_CALLBACK.
    // This passes the request down to the next miniFilter in the chain.

    return FLT_PREOP_SUCCESS_WITH_CALLBACK;
}



VOID
Chapter10DelProtectOperationStatusCallback (
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
                  ("Chapter10DelProtect!Chapter10DelProtectOperationStatusCallback: Entered\n") );

    PT_DBG_PRINT( PTDBG_TRACE_OPERATION_STATUS,
                  ("Chapter10DelProtect!Chapter10DelProtectOperationStatusCallback: Status=%08x ctx=%p IrpMj=%02x.%02x \"%s\"\n",
                   OperationStatus,
                   RequesterContext,
                   ParameterSnapshot->MajorFunction,
                   ParameterSnapshot->MinorFunction,
                   FltGetIrpName(ParameterSnapshot->MajorFunction)) );
}


FLT_POSTOP_CALLBACK_STATUS
Chapter10DelProtectPostOperation (
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
                  ("Chapter10DelProtect!Chapter10DelProtectPostOperation: Entered\n") );

    return FLT_POSTOP_FINISHED_PROCESSING;
}


FLT_PREOP_CALLBACK_STATUS
Chapter10DelProtectPreOperationNoPostOperation (
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
                  ("Chapter10DelProtect!Chapter10DelProtectPreOperationNoPostOperation: Entered\n") );

    // This template code does not do anything with the callbackData, but
    // rather returns FLT_PREOP_SUCCESS_NO_CALLBACK.
    // This passes the request down to the next miniFilter in the chain.

    return FLT_PREOP_SUCCESS_NO_CALLBACK;
}


BOOLEAN
Chapter10DelProtectDoRequestOperationStatus(
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
