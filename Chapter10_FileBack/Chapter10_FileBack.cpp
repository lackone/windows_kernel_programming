/*++

Module Name:

    Chapter10FileBack.c

Abstract:

    This is the main module of the Chapter10_FileBack miniFilter driver.

Environment:

    Kernel mode

--*/

#include <fltKernel.h>
#include <dontuse.h>
#include "Common.h"
#include "FltFileName.h"

#pragma prefast(disable:__WARNING_ENCODE_MEMBER_FUNCTION_POINTER, "Not valid for kernel mode drivers")


PFLT_FILTER gFilterHandle;
ULONG_PTR OperationStatusCtx = 1;

PFLT_PORT FilterPort;
PFLT_PORT SendClientPort;

struct FileBackPortMsg
{
    USHORT fileNameLen;
    WCHAR fileName[1];
};

#define PTDBG_TRACE_ROUTINES            0x00000001
#define PTDBG_TRACE_OPERATION_STATUS    0x00000002

ULONG gTraceFlags = 0;

#define DRIVER_CONTEXT_TAG 'dfc'

//定义一个上下文结构
struct FileContext
{
    Mutex lock;
    UNICODE_STRING fileName;
    BOOLEAN write;
};


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
Chapter10FileBackInstanceSetup (
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_ FLT_INSTANCE_SETUP_FLAGS Flags,
    _In_ DEVICE_TYPE VolumeDeviceType,
    _In_ FLT_FILESYSTEM_TYPE VolumeFilesystemType
    );

VOID
Chapter10FileBackInstanceTeardownStart (
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_ FLT_INSTANCE_TEARDOWN_FLAGS Flags
    );

VOID
Chapter10FileBackInstanceTeardownComplete (
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_ FLT_INSTANCE_TEARDOWN_FLAGS Flags
    );

NTSTATUS
Chapter10FileBackUnload (
    _In_ FLT_FILTER_UNLOAD_FLAGS Flags
    );

NTSTATUS
Chapter10FileBackInstanceQueryTeardown (
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_ FLT_INSTANCE_QUERY_TEARDOWN_FLAGS Flags
    );

FLT_PREOP_CALLBACK_STATUS
Chapter10FileBackPreOperation (
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _Flt_CompletionContext_Outptr_ PVOID *CompletionContext
    );

VOID
Chapter10FileBackOperationStatusCallback (
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_ PFLT_IO_PARAMETER_BLOCK ParameterSnapshot,
    _In_ NTSTATUS OperationStatus,
    _In_ PVOID RequesterContext
    );

FLT_POSTOP_CALLBACK_STATUS
Chapter10FileBackPostOperation (
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_opt_ PVOID CompletionContext,
    _In_ FLT_POST_OPERATION_FLAGS Flags
    );

FLT_PREOP_CALLBACK_STATUS
Chapter10FileBackPreOperationNoPostOperation (
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _Flt_CompletionContext_Outptr_ PVOID *CompletionContext
    );

BOOLEAN
Chapter10FileBackDoRequestOperationStatus(
    _In_ PFLT_CALLBACK_DATA Data
    );

EXTERN_C_END

//
//  Assign text sections for each routine.
//

#ifdef ALLOC_PRAGMA
#pragma alloc_text(INIT, DriverEntry)
#pragma alloc_text(PAGE, Chapter10FileBackUnload)
#pragma alloc_text(PAGE, Chapter10FileBackInstanceQueryTeardown)
#pragma alloc_text(PAGE, Chapter10FileBackInstanceSetup)
#pragma alloc_text(PAGE, Chapter10FileBackInstanceTeardownStart)
#pragma alloc_text(PAGE, Chapter10FileBackInstanceTeardownComplete)
#endif

//
//  operation registration
//

bool isBackDir(PCUNICODE_STRING dir)
{
    ULONG maxSize = 1024;

    if (dir->Length > maxSize)
    {
        return false;
    }

    auto copy = (WCHAR*)ExAllocatePoolWithTag(PagedPool, maxSize + sizeof(WCHAR), 0);
    if (!copy)
    {
        return false;
    }

    RtlZeroMemory(copy, maxSize + sizeof(WCHAR));
    wcsncpy_s(copy, 1 + maxSize / sizeof(WCHAR), dir->Buffer, dir->Length / sizeof(WCHAR));
    _wcslwr(copy); //转小写

    bool isBack = wcsstr(copy, L"\\pictures\\") || wcsstr(copy, L"\\documents\\");

    ExFreePoolWithTag(copy, 0);

    return isBack;
}

NTSTATUS backFile(PUNICODE_STRING fileName, PCFLT_RELATED_OBJECTS fltObjects)
{
    HANDLE targetFile = nullptr;
    HANDLE sourceFile = nullptr;
    IO_STATUS_BLOCK io;
    auto status = STATUS_SUCCESS;
    PVOID buf = nullptr;

    //获取文件大小，大小为0，则不需要备份
    LARGE_INTEGER fileSize;
    status = FsRtlGetFileSize(fltObjects->FileObject, &fileSize);
    if (!NT_SUCCESS(status) || fileSize.QuadPart == 0)
    {
        return status;
    }

    do 
    {
        //打开源文件
        OBJECT_ATTRIBUTES sourceAttr;
        InitializeObjectAttributes(&sourceAttr, fileName, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, nullptr, nullptr);

        status = FltCreateFile(
            fltObjects->Filter,
            fltObjects->Instance,
            &sourceFile,
            FILE_READ_DATA | SYNCHRONIZE,
            &sourceAttr,
            &io,
            nullptr,
            FILE_ATTRIBUTE_NORMAL,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            FILE_OPEN, //文件必须存在
            FILE_SYNCHRONOUS_IO_NONALERT, //同步操作
            nullptr,
            0,
            IO_IGNORE_SHARE_ACCESS_CHECK //忽略共享访问检查
        );

        if (!NT_SUCCESS(status))
        {
            break;
        }

        //创建备份流
        UNICODE_STRING targetFileName;
        WCHAR backStream[] = L":backup";
        targetFileName.MaximumLength = fileName->Length + sizeof(backStream);
        targetFileName.Buffer = (WCHAR*)ExAllocatePoolWithTag(PagedPool, targetFileName.MaximumLength, 0);
        if (!targetFileName.Buffer)
        {
            break;
        }

        RtlCopyUnicodeString(&targetFileName, fileName);
        RtlAppendUnicodeToString(&targetFileName, backStream);

		OBJECT_ATTRIBUTES targetAttr;
		InitializeObjectAttributes(&targetAttr, &targetFileName, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, nullptr, nullptr);

		status = FltCreateFile(
			fltObjects->Filter,
			fltObjects->Instance,
			&targetFile,
			GENERIC_WRITE | SYNCHRONIZE,
			&targetAttr,
			&io,
			nullptr,
			FILE_ATTRIBUTE_NORMAL,
			0,
			FILE_OVERWRITE_IF, //
			FILE_SYNCHRONOUS_IO_NONALERT, //同步操作
			nullptr,
			0,
			0
		);

        ExFreePoolWithTag(targetFileName.Buffer, 0);

		//分配一个相对小的缓冲区，并进行循环读写直到所有文件块都被复制
        ULONG size = 1 << 21; //2MB
        buf = ExAllocatePoolWithTag(PagedPool, size, 0);
        if (!buf)
        {
            status = STATUS_INSUFFICIENT_RESOURCES;
            break;
        }

        LARGE_INTEGER readOff = { 0 };
        LARGE_INTEGER writeOff = { 0 };

        ULONG bytes;
        auto saveSize = fileSize;
        while (fileSize.QuadPart > 0)
        {
            //读数据
            status = ZwReadFile(sourceFile, nullptr, nullptr, nullptr, &io, buf, (ULONG)min((LONGLONG)size, fileSize.QuadPart), &readOff, nullptr);
            if (!NT_SUCCESS(status))
            {
                break;
            }

            bytes = (ULONG)io.Information;

            status = ZwWriteFile(targetFile, nullptr, nullptr, nullptr, &io, buf, bytes, &writeOff, nullptr);
			if (!NT_SUCCESS(status))
			{
				break;
			}

            readOff.QuadPart += bytes;
            writeOff.QuadPart += bytes;
            fileSize.QuadPart -= bytes;
        }

		//因为我们可能是在覆盖前一个备份（可能比现在这个大），所以必须将文件的结束指针设置成当前的偏移
        FILE_END_OF_FILE_INFORMATION info;
        info.EndOfFile = saveSize;

        status = ZwSetInformationFile(targetFile, &io, &info, sizeof(info), FileEndOfFileInformation);
		if (!NT_SUCCESS(status))
		{
			break;
		}

    } while (0);

    if (buf)
    {
        ExFreePoolWithTag(buf, 0);
    }
    if (sourceFile)
    {
        FltClose(sourceFile);
    }
    if (targetFile)
    {
        FltClose(targetFile);
    }

    return status;
}

FLT_POSTOP_CALLBACK_STATUS FLTAPI fileBackPostCreate(
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_opt_ PVOID CompletionContext,
    _In_ FLT_POST_OPERATION_FLAGS Flags
)
{
    UNREFERENCED_PARAMETER(Flags);
    UNREFERENCED_PARAMETER(CompletionContext);

    auto& params = Data->Iopb->Parameters.Create;

    if (Data->RequestorMode == KernelMode ||   //请求来自于内核
        (params.SecurityContext->DesiredAccess & FILE_WRITE_DATA) == 0 ||  //不是以写访问打开文件
        Data->IoStatus.Information == FILE_DOES_NOT_EXIST //文件不存在，新文件
    )
    {
        return FLT_POSTOP_FINISHED_PROCESSING;
    }

    FilterFileNameInformation fileNameInfo(Data);
    if (!fileNameInfo)
    {
        return FLT_POSTOP_FINISHED_PROCESSING;
    }
    if (!NT_SUCCESS(fileNameInfo.Parse()))
    {
        return FLT_POSTOP_FINISHED_PROCESSING;
    }

    if (!isBackDir(&fileNameInfo->ParentDir))
    {
        return FLT_POSTOP_FINISHED_PROCESSING;
    }

	//如果文件打开的不是默认的流，我们就不应该尝试备份什么。我们只备份默认的数据流
    if (fileNameInfo->Stream.Length > 0)
    {
        return FLT_POSTOP_FINISHED_PROCESSING;
    }

    //分配文件上下文
    FileContext* context;
    auto status = FltAllocateContext(FltObjects->Filter, FLT_FILE_CONTEXT, sizeof(FileContext), PagedPool, (PFLT_CONTEXT*)&context);
    if (!NT_SUCCESS(status))
    {
        return FLT_POSTOP_FINISHED_PROCESSING;
    }

    context->write = false;
    context->fileName.MaximumLength = fileNameInfo->Name.Length;
    context->fileName.Buffer = (WCHAR*)ExAllocatePoolWithTag(PagedPool, fileNameInfo->Name.Length, 0);
    if (!context->fileName.Buffer)
    {
        FltReleaseContext(context);
        return FLT_POSTOP_FINISHED_PROCESSING;
    }

    RtlCopyUnicodeString(&context->fileName, &fileNameInfo->Name);

	//像ZwWriteFile和ZwReadFile这样的I/OAPI只能在IRQL PASSIVE_LEVEL（0）时调用。
    //获取快速互斥量会把IRQL提高到IRQL APC_LEVEL（1），如果这时调用I / O API就会造成死锁
    context->lock.Init();

    //设置上下文
    status = FltSetFileContext(FltObjects->Instance, FltObjects->FileObject, FLT_SET_CONTEXT_KEEP_IF_EXISTS, context, nullptr);

	if (!NT_SUCCESS(status))
    {
        ExFreePoolWithTag(context->fileName.Buffer, 0);
	}

	FltReleaseContext(context);

    return FLT_POSTOP_FINISHED_PROCESSING;
}

FLT_POSTOP_CALLBACK_STATUS FLTAPI fileBackPostCleanup(
	_Inout_ PFLT_CALLBACK_DATA Data,
	_In_ PCFLT_RELATED_OBJECTS FltObjects,
	_In_opt_ PVOID CompletionContext,
	_In_ FLT_POST_OPERATION_FLAGS Flags
)
{
    UNREFERENCED_PARAMETER(Data);
	UNREFERENCED_PARAMETER(Flags);
	UNREFERENCED_PARAMETER(CompletionContext);

	FileContext* context;

	auto status = FltGetFileContext(FltObjects->Instance, FltObjects->FileObject, (PFLT_CONTEXT*)&context);
	if (!NT_SUCCESS(status) || context == nullptr) 
    {
		return FLT_POSTOP_FINISHED_PROCESSING;
	}

    if (context->fileName.Buffer)
    {
        ExFreePoolWithTag(context->fileName.Buffer, 0);
    }
		
	FltReleaseContext(context);
	FltDeleteContext(context);

    return FLT_POSTOP_FINISHED_PROCESSING;
}

FLT_PREOP_CALLBACK_STATUS FLTAPI fileBackPreWrite(
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _Outptr_result_maybenull_ PVOID* CompletionContext
    )
{
    UNREFERENCED_PARAMETER(Data);
    UNREFERENCED_PARAMETER(CompletionContext);

	//在进行真正的写操作之前，先复制一份文件数据
    FileContext* context;
    auto status = FltGetFileContext(FltObjects->Instance, FltObjects->FileObject, (PFLT_CONTEXT*)&context);
    if (!NT_SUCCESS(status) || context == nullptr)
    {
        return FLT_PREOP_SUCCESS_NO_CALLBACK;
    }

    AutoLock<Mutex> lock(context->lock);

    if (!context->write)
    {
        status = backFile(&context->fileName, FltObjects);
        if (!NT_SUCCESS(status))
        {
            DbgPrintEx(77, 0, "backup error %x \n", status);
        }
        else
        {
			// 给客户端发送消息
			if (SendClientPort) 
            {
				USHORT nameLen = context->fileName.Length;
				USHORT len = sizeof(FileBackPortMsg) + nameLen;
				auto msg = (FileBackPortMsg*)ExAllocatePoolWithTag(PagedPool, len, 0);
				if (msg) 
                {
					msg->fileNameLen = nameLen / sizeof(WCHAR);
					RtlCopyMemory(msg->fileName, context->fileName.Buffer, nameLen);

					LARGE_INTEGER timeout;
					timeout.QuadPart = -10000 * 100;	// 100msec
					FltSendMessage(gFilterHandle, &SendClientPort, msg, len, nullptr, nullptr, &timeout);

					ExFreePoolWithTag(msg, 0);
				}
			}
        }

        context->write = true;
    }

    FltReleaseContext(context);

    return FLT_PREOP_SUCCESS_NO_CALLBACK;
}

CONST FLT_OPERATION_REGISTRATION Callbacks[] = {
    { IRP_MJ_CREATE, 0, nullptr, fileBackPostCreate},
    { IRP_MJ_WRITE, FLTFL_OPERATION_REGISTRATION_SKIP_PAGING_IO, fileBackPreWrite, nullptr},
    { IRP_MJ_CLEANUP, 0, nullptr, fileBackPostCleanup},

#if 0 // TODO - List all of the requests to filter.
    { IRP_MJ_CREATE,
      0,
      Chapter10FileBackPreOperation,
      Chapter10FileBackPostOperation },

    { IRP_MJ_CREATE_NAMED_PIPE,
      0,
      Chapter10FileBackPreOperation,
      Chapter10FileBackPostOperation },

    { IRP_MJ_CLOSE,
      0,
      Chapter10FileBackPreOperation,
      Chapter10FileBackPostOperation },

    { IRP_MJ_READ,
      0,
      Chapter10FileBackPreOperation,
      Chapter10FileBackPostOperation },

    { IRP_MJ_WRITE,
      0,
      Chapter10FileBackPreOperation,
      Chapter10FileBackPostOperation },

    { IRP_MJ_QUERY_INFORMATION,
      0,
      Chapter10FileBackPreOperation,
      Chapter10FileBackPostOperation },

    { IRP_MJ_SET_INFORMATION,
      0,
      Chapter10FileBackPreOperation,
      Chapter10FileBackPostOperation },

    { IRP_MJ_QUERY_EA,
      0,
      Chapter10FileBackPreOperation,
      Chapter10FileBackPostOperation },

    { IRP_MJ_SET_EA,
      0,
      Chapter10FileBackPreOperation,
      Chapter10FileBackPostOperation },

    { IRP_MJ_FLUSH_BUFFERS,
      0,
      Chapter10FileBackPreOperation,
      Chapter10FileBackPostOperation },

    { IRP_MJ_QUERY_VOLUME_INFORMATION,
      0,
      Chapter10FileBackPreOperation,
      Chapter10FileBackPostOperation },

    { IRP_MJ_SET_VOLUME_INFORMATION,
      0,
      Chapter10FileBackPreOperation,
      Chapter10FileBackPostOperation },

    { IRP_MJ_DIRECTORY_CONTROL,
      0,
      Chapter10FileBackPreOperation,
      Chapter10FileBackPostOperation },

    { IRP_MJ_FILE_SYSTEM_CONTROL,
      0,
      Chapter10FileBackPreOperation,
      Chapter10FileBackPostOperation },

    { IRP_MJ_DEVICE_CONTROL,
      0,
      Chapter10FileBackPreOperation,
      Chapter10FileBackPostOperation },

    { IRP_MJ_INTERNAL_DEVICE_CONTROL,
      0,
      Chapter10FileBackPreOperation,
      Chapter10FileBackPostOperation },

    { IRP_MJ_SHUTDOWN,
      0,
      Chapter10FileBackPreOperationNoPostOperation,
      NULL },                               //post operations not supported

    { IRP_MJ_LOCK_CONTROL,
      0,
      Chapter10FileBackPreOperation,
      Chapter10FileBackPostOperation },

    { IRP_MJ_CLEANUP,
      0,
      Chapter10FileBackPreOperation,
      Chapter10FileBackPostOperation },

    { IRP_MJ_CREATE_MAILSLOT,
      0,
      Chapter10FileBackPreOperation,
      Chapter10FileBackPostOperation },

    { IRP_MJ_QUERY_SECURITY,
      0,
      Chapter10FileBackPreOperation,
      Chapter10FileBackPostOperation },

    { IRP_MJ_SET_SECURITY,
      0,
      Chapter10FileBackPreOperation,
      Chapter10FileBackPostOperation },

    { IRP_MJ_QUERY_QUOTA,
      0,
      Chapter10FileBackPreOperation,
      Chapter10FileBackPostOperation },

    { IRP_MJ_SET_QUOTA,
      0,
      Chapter10FileBackPreOperation,
      Chapter10FileBackPostOperation },

    { IRP_MJ_PNP,
      0,
      Chapter10FileBackPreOperation,
      Chapter10FileBackPostOperation },

    { IRP_MJ_ACQUIRE_FOR_SECTION_SYNCHRONIZATION,
      0,
      Chapter10FileBackPreOperation,
      Chapter10FileBackPostOperation },

    { IRP_MJ_RELEASE_FOR_SECTION_SYNCHRONIZATION,
      0,
      Chapter10FileBackPreOperation,
      Chapter10FileBackPostOperation },

    { IRP_MJ_ACQUIRE_FOR_MOD_WRITE,
      0,
      Chapter10FileBackPreOperation,
      Chapter10FileBackPostOperation },

    { IRP_MJ_RELEASE_FOR_MOD_WRITE,
      0,
      Chapter10FileBackPreOperation,
      Chapter10FileBackPostOperation },

    { IRP_MJ_ACQUIRE_FOR_CC_FLUSH,
      0,
      Chapter10FileBackPreOperation,
      Chapter10FileBackPostOperation },

    { IRP_MJ_RELEASE_FOR_CC_FLUSH,
      0,
      Chapter10FileBackPreOperation,
      Chapter10FileBackPostOperation },

    { IRP_MJ_FAST_IO_CHECK_IF_POSSIBLE,
      0,
      Chapter10FileBackPreOperation,
      Chapter10FileBackPostOperation },

    { IRP_MJ_NETWORK_QUERY_OPEN,
      0,
      Chapter10FileBackPreOperation,
      Chapter10FileBackPostOperation },

    { IRP_MJ_MDL_READ,
      0,
      Chapter10FileBackPreOperation,
      Chapter10FileBackPostOperation },

    { IRP_MJ_MDL_READ_COMPLETE,
      0,
      Chapter10FileBackPreOperation,
      Chapter10FileBackPostOperation },

    { IRP_MJ_PREPARE_MDL_WRITE,
      0,
      Chapter10FileBackPreOperation,
      Chapter10FileBackPostOperation },

    { IRP_MJ_MDL_WRITE_COMPLETE,
      0,
      Chapter10FileBackPreOperation,
      Chapter10FileBackPostOperation },

    { IRP_MJ_VOLUME_MOUNT,
      0,
      Chapter10FileBackPreOperation,
      Chapter10FileBackPostOperation },

    { IRP_MJ_VOLUME_DISMOUNT,
      0,
      Chapter10FileBackPreOperation,
      Chapter10FileBackPostOperation },

#endif // TODO

    { IRP_MJ_OPERATION_END }
};

//
//  This defines what we want to filter with FltMgr
//

const FLT_CONTEXT_REGISTRATION Contexts[] = {
	// 上下文数组之中进行注册
    { FLT_FILE_CONTEXT, 0, nullptr, sizeof(FileContext), DRIVER_CONTEXT_TAG },
    { FLT_CONTEXT_END }
};

CONST FLT_REGISTRATION FilterRegistration = {

    sizeof( FLT_REGISTRATION ),         //  Size
    FLT_REGISTRATION_VERSION,           //  Version
    0,                                  //  Flags

    Contexts,                               //  Context
    Callbacks,                          //  Operation callbacks

    Chapter10FileBackUnload,                           //  MiniFilterUnload

    Chapter10FileBackInstanceSetup,                    //  InstanceSetup
    Chapter10FileBackInstanceQueryTeardown,            //  InstanceQueryTeardown
    Chapter10FileBackInstanceTeardownStart,            //  InstanceTeardownStart
    Chapter10FileBackInstanceTeardownComplete,         //  InstanceTeardownComplete

    NULL,                               //  GenerateFileName
    NULL,                               //  GenerateDestinationFileName
    NULL                                //  NormalizeNameComponent

};



NTSTATUS
Chapter10FileBackInstanceSetup (
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

    PAGED_CODE();

    PT_DBG_PRINT( PTDBG_TRACE_ROUTINES,
                  ("Chapter10FileBack!Chapter10FileBackInstanceSetup: Entered\n") );

    //如果卷的类型不是NTFS,则不附加
    if (VolumeFilesystemType != FLT_FSTYPE_NTFS)
    {
        return STATUS_FLT_DO_NOT_ATTACH;
    }

    return STATUS_SUCCESS;
}


NTSTATUS
Chapter10FileBackInstanceQueryTeardown (
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
                  ("Chapter10FileBack!Chapter10FileBackInstanceQueryTeardown: Entered\n") );

    return STATUS_SUCCESS;
}


VOID
Chapter10FileBackInstanceTeardownStart (
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
                  ("Chapter10FileBack!Chapter10FileBackInstanceTeardownStart: Entered\n") );
}


VOID
Chapter10FileBackInstanceTeardownComplete (
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
                  ("Chapter10FileBack!Chapter10FileBackInstanceTeardownComplete: Entered\n") );
}


/*************************************************************************
    MiniFilter initialization and unload routines.
*************************************************************************/

NTSTATUS FLTAPI connectNotify (
    _In_ PFLT_PORT ClientPort,
    _In_opt_ PVOID ServerPortCookie,
    _In_reads_bytes_opt_(SizeOfContext) PVOID ConnectionContext,
    _In_ ULONG SizeOfContext,
    _Outptr_result_maybenull_ PVOID* ConnectionPortCookie
    )
{
	UNREFERENCED_PARAMETER(ServerPortCookie);
	UNREFERENCED_PARAMETER(ConnectionContext);
	UNREFERENCED_PARAMETER(SizeOfContext);
	UNREFERENCED_PARAMETER(ConnectionPortCookie);


	//ClientPort是指向客户端口的唯一句柄，驱动程序必须保存它，并且无论何时需要与客户程序通信都会用到它。
    SendClientPort = ClientPort;

    return STATUS_SUCCESS;
}

VOID FLTAPI disconnectNotify (
    _In_opt_ PVOID ConnectionCookie
    )
{
    UNREFERENCED_PARAMETER(ConnectionCookie);

	FltCloseClientPort(gFilterHandle, &SendClientPort);
	SendClientPort = nullptr;
}

NTSTATUS FLTAPI messageNotify (
    _In_opt_ PVOID PortCookie,
    _In_reads_bytes_opt_(InputBufferLength) PVOID InputBuffer,
    _In_ ULONG InputBufferLength,
    _Out_writes_bytes_to_opt_(OutputBufferLength, *ReturnOutputBufferLength) PVOID OutputBuffer,
    _In_ ULONG OutputBufferLength,
    _Out_ PULONG ReturnOutputBufferLength
    )
{
	UNREFERENCED_PARAMETER(PortCookie);
	UNREFERENCED_PARAMETER(InputBuffer);
	UNREFERENCED_PARAMETER(InputBufferLength);
	UNREFERENCED_PARAMETER(OutputBuffer);
	UNREFERENCED_PARAMETER(OutputBufferLength);
	UNREFERENCED_PARAMETER(ReturnOutputBufferLength);

    return STATUS_SUCCESS;
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

    PT_DBG_PRINT( PTDBG_TRACE_ROUTINES,
                  ("Chapter10FileBack!DriverEntry: Entered\n") );

    //
    //  Register with FltMgr to tell it our callback routines
    //

    status = FltRegisterFilter( DriverObject,
                                &FilterRegistration,
                                &gFilterHandle );

    FLT_ASSERT( NT_SUCCESS( status ) );

    if (!NT_SUCCESS( status )) 
    {
        return status;
    }

    do 
    {
        //创建通信端口
        UNICODE_STRING portName = RTL_CONSTANT_STRING(L"\\FileBackPort");
        OBJECT_ATTRIBUTES portAttr = { 0 };
        PSECURITY_DESCRIPTOR sd;

        status = FltBuildDefaultSecurityDescriptor(&sd, FLT_PORT_ALL_ACCESS);
        if (!NT_SUCCESS(status))
        {
            break;
        }

        InitializeObjectAttributes(&portAttr, &portName, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, nullptr, sd);

        status = FltCreateCommunicationPort(gFilterHandle, &FilterPort, &portAttr, nullptr, connectNotify, disconnectNotify, messageNotify, 1);

        FltFreeSecurityDescriptor(sd);

		if (!NT_SUCCESS(status))
		{
			break;
		}

        status = FltStartFiltering(gFilterHandle);

    } while (0);

	if (!NT_SUCCESS(status)) 
    {
		FltUnregisterFilter(gFilterHandle);
	}

    return status;
}

NTSTATUS
Chapter10FileBackUnload (
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
                  ("Chapter10FileBack!Chapter10FileBackUnload: Entered\n") );

    //在过滤器的卸载例程中，我们必须关闭过滤器的通信端口
    FltCloseCommunicationPort(FilterPort);

    FltUnregisterFilter( gFilterHandle );

    return STATUS_SUCCESS;
}


/*************************************************************************
    MiniFilter callback routines.
*************************************************************************/
FLT_PREOP_CALLBACK_STATUS
Chapter10FileBackPreOperation (
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
                  ("Chapter10FileBack!Chapter10FileBackPreOperation: Entered\n") );

    //
    //  See if this is an operation we would like the operation status
    //  for.  If so request it.
    //
    //  NOTE: most filters do NOT need to do this.  You only need to make
    //        this call if, for example, you need to know if the oplock was
    //        actually granted.
    //

    if (Chapter10FileBackDoRequestOperationStatus( Data )) {

        status = FltRequestOperationStatusCallback( Data,
                                                    Chapter10FileBackOperationStatusCallback,
                                                    (PVOID)(++OperationStatusCtx) );
        if (!NT_SUCCESS(status)) {

            PT_DBG_PRINT( PTDBG_TRACE_OPERATION_STATUS,
                          ("Chapter10FileBack!Chapter10FileBackPreOperation: FltRequestOperationStatusCallback Failed, status=%08x\n",
                           status) );
        }
    }

    // This template code does not do anything with the callbackData, but
    // rather returns FLT_PREOP_SUCCESS_WITH_CALLBACK.
    // This passes the request down to the next miniFilter in the chain.

    return FLT_PREOP_SUCCESS_WITH_CALLBACK;
}



VOID
Chapter10FileBackOperationStatusCallback (
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
                  ("Chapter10FileBack!Chapter10FileBackOperationStatusCallback: Entered\n") );

    PT_DBG_PRINT( PTDBG_TRACE_OPERATION_STATUS,
                  ("Chapter10FileBack!Chapter10FileBackOperationStatusCallback: Status=%08x ctx=%p IrpMj=%02x.%02x \"%s\"\n",
                   OperationStatus,
                   RequesterContext,
                   ParameterSnapshot->MajorFunction,
                   ParameterSnapshot->MinorFunction,
                   FltGetIrpName(ParameterSnapshot->MajorFunction)) );
}


FLT_POSTOP_CALLBACK_STATUS
Chapter10FileBackPostOperation (
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
                  ("Chapter10FileBack!Chapter10FileBackPostOperation: Entered\n") );

    return FLT_POSTOP_FINISHED_PROCESSING;
}


FLT_PREOP_CALLBACK_STATUS
Chapter10FileBackPreOperationNoPostOperation (
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
                  ("Chapter10FileBack!Chapter10FileBackPreOperationNoPostOperation: Entered\n") );

    // This template code does not do anything with the callbackData, but
    // rather returns FLT_PREOP_SUCCESS_NO_CALLBACK.
    // This passes the request down to the next miniFilter in the chain.

    return FLT_PREOP_SUCCESS_NO_CALLBACK;
}


BOOLEAN
Chapter10FileBackDoRequestOperationStatus(
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
