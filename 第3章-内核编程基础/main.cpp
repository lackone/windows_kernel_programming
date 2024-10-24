#include <ntifs.h>
#include <ntddk.h>
#include <intrin.h>

#define UP(P) UNREFERENCED_PARAMETER(P)

typedef struct _MyData
{
	ULONG data_a;
	LIST_ENTRY list;
	ULONG data_b;
} MyData, * PMyData;

VOID DriverUnload(PDRIVER_OBJECT DriverObject)
{
	UP(DriverObject);

	DbgPrintEx(77, 0, "DriverUnload \r\n");
}

/**
 * �����ַ���
 */
VOID TestString(PUNICODE_STRING reg)
{
	UNICODE_STRING str = { 0 };

	//�����ڴ�
	str.Buffer = (WCHAR*)ExAllocatePoolWithTag(PagedPool, reg->Length, 'str');

	if (str.Buffer == nullptr)
	{
		DbgPrintEx(77, 0, "�����ڴ�ʧ��\n");
		return;
	}

	str.MaximumLength = reg->Length;

	RtlCopyUnicodeString(&str, reg);

	DbgPrintEx(77, 0, "%wZ \n", &str);
}

/**
 * ��������
 */
VOID TestLink()
{
	LIST_ENTRY head;

	//��ʼ����ͷ
	InitializeListHead(&head);

	MyData a = { 1, LIST_ENTRY{}, 2 };
	MyData b = { 3, LIST_ENTRY{}, 4 };
	MyData c = { 5, LIST_ENTRY{}, 6 };

	//����
	InsertHeadList(&head, &(a.list));
	InsertHeadList(&head, &(b.list));
	InsertHeadList(&head, &(c.list));

	PLIST_ENTRY tmp = head.Flink;

	//����
	while (tmp != &head)
	{
		PMyData md = CONTAINING_RECORD(tmp, MyData, list);

		DbgPrintEx(77, 0, "%d %d \n", md->data_a, md->data_b);

		tmp = tmp->Flink;
	}
}

extern "C" NTSTATUS DriverEntry(PDRIVER_OBJECT driver, PUNICODE_STRING reg)
{
	UP(reg);

	TestString(reg);

	TestLink();

	driver->DriverUnload = DriverUnload;
	return STATUS_SUCCESS;
}