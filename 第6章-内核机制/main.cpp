#include <ntifs.h>
#include <ntddk.h>
#include <intrin.h>

#define UP(P) UNREFERENCED_PARAMETER(P)

template<typename T = void>
struct kunique_ptr
{
	kunique_ptr(T* p = nullptr) : _p(p) 
	{

	}
	~kunique_ptr()
	{
		if (_p)
		{
			DbgPrintEx(77, 0, "我被释放了\n");
			ExFreePool(_p);
		}
	}
	T* operator->() const
	{
		return _p;
	}
	T& operator*() const
	{
		return *p;
	}
private:
	T* _p;
};

struct MyDATA
{
	ULONG data1;
	HANDLE data2;
};

KTIMER timer;
KDPC dpc;
KMUTEX mutex;

VOID DriverUnload(PDRIVER_OBJECT DriverObject)
{
	UP(DriverObject);

	DbgPrintEx(77, 0, "DriverUnload \r\n");

	KeCancelTimer(&timer);
}

/**
 * dpc的调用与线程无关
 */
VOID timeCallback(
	_In_ struct _KDPC* Dpc,
	_In_opt_ PVOID DeferredContext,
	_In_opt_ PVOID SystemArgument1,
	_In_opt_ PVOID SystemArgument2
)
{
	UP(Dpc);
	UP(SystemArgument1);
	UP(SystemArgument2);

	LARGE_INTEGER time = { 0 };
	time.QuadPart = -10000 * (ULONG_PTR)DeferredContext;

	DbgPrintEx(77, 0, "irql = %d \n", KeGetCurrentIrql());

	KeSetTimer(&timer, time, &dpc);
}

VOID initDpc(ULONG msec)
{
	LARGE_INTEGER time = { 0 };
	time.QuadPart = -10000 * msec;

	KeInitializeTimer(&timer);
	KeInitializeDpc(&dpc, timeCallback, (PVOID)msec);

	KeSetTimer(&timer, time, &dpc);
}

VOID testSEH()
{
	__try
	{
		ULONG a = 1;
		ULONG b = 0;
		ULONG c = a / b;

		DbgPrintEx(77, 0, "%d \n", c);
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		DbgPrintEx(77, 0, "哈，我出现异常了\n");
	}

	PVOID mem = NULL;
	__try
	{
		mem = ExAllocatePoolWithTag(PagedPool, 1024, 0);
	}
	__finally
	{
		DbgPrintEx(77, 0, "我肯定会被执行\n");
		if (mem)
		{
			ExFreePool(mem);
		}
	}
}

VOID testRAII()
{
	kunique_ptr<MyDATA> data((MyDATA*)ExAllocatePoolWithTag(PagedPool, sizeof(MyDATA), 0));

	data->data1 = 10;

	DbgPrintEx(77, 0, "%d \n", data->data1);
}

VOID testMutex()
{
	KeWaitForSingleObject(&mutex, Executive, KernelMode, FALSE, nullptr);

	//访问共享数据。。。。

	KeReleaseMutex(&mutex, FALSE);
}

/**
 * 
 用户模式APC
 这些APC仅在线程进入警报状态时才在用户模式的IRQL PASSIVE_LEVEL上运行。
 通常会通过调用如SleepEx、WaitForSingleObjectEx、WaitForMultipleObjectsEx以及类似的API来达到此目的。
 这些函数的最后一个参数设置成TRUE时可以使线程进入警报状态。在警报状态下，线程会检查其APC队列，如果不是空的—其中的APC就会被执行，直到队列为空。

 普通内核模式APC
 它们在内核模式下的IRQL PASSIVE_LEVEL中执行，能够抢占用户模式代码和用户模式APC。

 特殊内核APC
 它们在内核模式下的IRQL APC_LEVEL（1）中执行，能够抢占用户模式代码、普通内核APC和用户模式APC，
 这些APC被I/O系统用来完成I/O操作。它们常见的应用场景会在下一章讨论。
 */

extern "C" NTSTATUS DriverEntry(PDRIVER_OBJECT driver, PUNICODE_STRING reg)
{
	UP(reg);

	initDpc(1000);

	testSEH();

	testRAII();

	KeInitializeMutex(&mutex, 0);
	testMutex();

	driver->DriverUnload = DriverUnload;
	return STATUS_SUCCESS;
}