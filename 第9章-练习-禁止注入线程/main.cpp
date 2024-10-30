#include <ntifs.h>
#include <ntddk.h>
#include <intrin.h>

#define UP(P) UNREFERENCED_PARAMETER(P)

typedef NTSTATUS(NTAPI* PspTerminateThreadByPointer)(
	IN PETHREAD Thread,
	IN NTSTATUS ExitStatus,
	IN BOOLEAN DirectTerminate
	);

extern "C" PVOID PsGetProcessDebugPort(
	__in PEPROCESS Process
);

ULONG_PTR getPspTerminateThreadByPointer()
{
	UNICODE_STRING name = { 0 };
	RtlInitUnicodeString(&name, L"PsTerminateSystemThread");

	PUCHAR func = (PUCHAR)MmGetSystemRoutineAddress(&name);

	ULONG_PTR addr = 0;

	for (ULONG i = 0; i < 100; i++)
	{
		if (func[i] == 0xE8)
		{
			LONG offset = *(PLONG)(func + i + 1);
			addr = (ULONG_PTR)func + i + 5 + offset;
			break;
		}
	}

	return addr;
}

PspTerminateThreadByPointer myPspTerminateThreadByPointer = nullptr;

VOID threadNotify(
	_In_ HANDLE ProcessId,
	_In_ HANDLE ThreadId,
	_In_ BOOLEAN Create
)
{
	if (Create)
	{
		HANDLE curPid = PsGetCurrentProcessId();
		PUNICODE_STRING curName = { 0 };
		PUNICODE_STRING tarName = { 0 };
		PEPROCESS curProcess = nullptr;
		PEPROCESS tarProcess = nullptr;
		WCHAR* buf = nullptr;
		PETHREAD thread = nullptr;

		do 
		{
			curProcess = PsGetCurrentProcess();
			if (!curProcess)
			{
				break;
			}
			auto status = SeLocateProcessImageName(curProcess, &curName);
			if (!NT_SUCCESS(status))
			{
				break;
			}

			status = PsLookupThreadByThreadId(ThreadId, &thread);
			if (!NT_SUCCESS(status))
			{
				break;
			}

			status = PsLookupProcessByProcessId(ProcessId, &tarProcess);
			if (!NT_SUCCESS(status))
			{
				break;
			}
			status = SeLocateProcessImageName(tarProcess, &tarName);
			if (!NT_SUCCESS(status))
			{
				break;
			}

			if (PsGetProcessDebugPort(tarProcess) != NULL)
			{
				// 进程处于调试状态
				break;
			}

			//判断是否是系统进程创建，system的进程ID一般为4
			if (curPid == (HANDLE)4)
			{
				break;
			}

			if (curName->Length > 0)
			{
				auto len = curName->Length / sizeof(WCHAR);
				buf = (WCHAR*)ExAllocatePoolWithTag(PagedPool, curName->Length + sizeof(WCHAR), 0);
				if (!buf)
				{
					break;
				}

				wcsncpy(buf, curName->Buffer, len);
				buf[len] = L'\0';
				_wcsupr(buf);

				DbgPrintEx(77, 0, "%ws \n", buf);

				if (wcsstr(buf, L"SYSTEM.EXE") || wcsstr(buf, L"SVCHOST.EXE") || wcsstr(buf, L"SEARCHINDEXER.EXE"))
				{
					break;
				}
			}

			DbgPrintEx(77, 0, "进程[%d][%wZ] 向进程[%d][%wZ] 注入线程 %d \n", curPid, curName, ProcessId, tarName, ThreadId);

			if (curPid != ProcessId)
			{
				DbgPrintEx(77, 0, "发现注入 %d %d \n", curPid, ProcessId);
				status = myPspTerminateThreadByPointer(thread, 0, TRUE);
				if (!NT_SUCCESS(status))
				{
					DbgPrintEx(77, 0, "杀死线程失败 %x \n", status);
					break;
				}
				DbgPrintEx(77, 0, "杀死线程 %d \n", ThreadId);
			}

		} while (0);
		
		if (thread)
		{
			ObDereferenceObject(thread);
		}
		if (tarProcess)
		{
			ObDereferenceObject(tarProcess);
		}
		if (curName)
		{
			ExFreePoolWithTag(curName, 0);
		}
		if (tarName)
		{
			ExFreePoolWithTag(tarName, 0);
		}
		if (buf)
		{
			ExFreePoolWithTag(buf, 0);
		}
	}
}

VOID DriverUnload(PDRIVER_OBJECT DriverObject)
{
	UP(DriverObject);

	PsRemoveCreateThreadNotifyRoutine(threadNotify);

	DbgPrintEx(77, 0, "DriverUnload \r\n");
}

extern "C" NTSTATUS DriverEntry(PDRIVER_OBJECT driver, PUNICODE_STRING reg)
{
	DbgBreakPoint();

	UP(reg);

	auto status = PsSetCreateThreadNotifyRoutine(threadNotify);
	if (!NT_SUCCESS(status))
	{
		return status;
	}

	myPspTerminateThreadByPointer = (PspTerminateThreadByPointer)getPspTerminateThreadByPointer();

	DbgPrintEx(77, 0, "PspTerminateThreadByPointer = %llx \n", myPspTerminateThreadByPointer);

	driver->DriverUnload = DriverUnload;
	return STATUS_SUCCESS;
}