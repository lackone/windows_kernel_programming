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
			DbgPrintEx(77, 0, "�ұ��ͷ���\n");
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
 * dpc�ĵ������߳��޹�
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
		DbgPrintEx(77, 0, "�����ҳ����쳣��\n");
	}

	PVOID mem = NULL;
	__try
	{
		mem = ExAllocatePoolWithTag(PagedPool, 1024, 0);
	}
	__finally
	{
		DbgPrintEx(77, 0, "�ҿ϶��ᱻִ��\n");
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

	//���ʹ������ݡ�������

	KeReleaseMutex(&mutex, FALSE);
}

/**
 * 
 �û�ģʽAPC
 ��ЩAPC�����߳̽��뾯��״̬ʱ�����û�ģʽ��IRQL PASSIVE_LEVEL�����С�
 ͨ����ͨ��������SleepEx��WaitForSingleObjectEx��WaitForMultipleObjectsEx�Լ����Ƶ�API���ﵽ��Ŀ�ġ�
 ��Щ���������һ���������ó�TRUEʱ����ʹ�߳̽��뾯��״̬���ھ���״̬�£��̻߳�����APC���У�������ǿյġ����е�APC�ͻᱻִ�У�ֱ������Ϊ�ա�

 ��ͨ�ں�ģʽAPC
 �������ں�ģʽ�µ�IRQL PASSIVE_LEVEL��ִ�У��ܹ���ռ�û�ģʽ������û�ģʽAPC��

 �����ں�APC
 �������ں�ģʽ�µ�IRQL APC_LEVEL��1����ִ�У��ܹ���ռ�û�ģʽ���롢��ͨ�ں�APC���û�ģʽAPC��
 ��ЩAPC��I/Oϵͳ�������I/O���������ǳ�����Ӧ�ó���������һ�����ۡ�
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