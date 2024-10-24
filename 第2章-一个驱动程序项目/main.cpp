#include <ntifs.h>
#include <ntddk.h>

//UNREFERENCED_PARAMETER����̫��������һ���̵�
#define UP(P) UNREFERENCED_PARAMETER(P)

VOID DriverUnload(_In_ struct _DRIVER_OBJECT* DriverObject)
{
	UP(DriverObject);

	KdPrint(("driver unload success \n"));
}

/**
 * ���԰�DriverEntry������������ġ�main����������������ᱻϵͳ�߳���IRQLPASSIVE_LEVEL��0���ϵ���
 * 
 * DriverEntry�����������C���Ե����ӷ�ʽ������C++����Ĭ�ϵķ�ʽ����C�ġ�
 * 
 * ������������
 * sc create FirstDriver type= kernel binPath= c:\FirstDriver.sys
 * sc start FirstDriver
 * sc stop FirstDriver
 * 
 * �򿪲���ǩ��ģʽ
 * bcdedit /set testsigning on
 * 
 * 
 * HKLM\SYSTEM\CurrentControlSet\Control\Session Manager������ Debug Print Filter�ļ�
 * ����һ������DEFAULT��DWORDֵ�����Ǽ�������Ǹ�Ĭ��ֵ��������ֵΪ8
 */
extern "C" NTSTATUS DriverEntry(PDRIVER_OBJECT driver, PUNICODE_STRING reg)
{
	//��������Ĭ�����óɽ�������Ϊ����Դ�
	//ͨ��UNREFERENCED_PARAMETER��������
	UP(driver);
	UP(reg);

	//�����������ӵ��һ��Unload���̣�����������������ڴ���ж��ʱ�Զ��õ����á�
	driver->DriverUnload = DriverUnload;

	KdPrint(("driver install success \n"));
	
	return STATUS_SUCCESS;
}