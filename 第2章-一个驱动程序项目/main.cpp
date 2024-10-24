#include <ntifs.h>
#include <ntddk.h>

//UNREFERENCED_PARAMETER宏字太长，定义一个短的
#define UP(P) UNREFERENCED_PARAMETER(P)

VOID DriverUnload(_In_ struct _DRIVER_OBJECT* DriverObject)
{
	UP(DriverObject);

	KdPrint(("driver unload success \n"));
}

/**
 * 可以把DriverEntry当作驱动程序的“main”函数。这个函数会被系统线程在IRQLPASSIVE_LEVEL（0）上调用
 * 
 * DriverEntry函数必须具有C语言的链接方式，但是C++编译默认的方式不是C的。
 * 
 * 部署驱动程序
 * sc create FirstDriver type= kernel binPath= c:\FirstDriver.sys
 * sc start FirstDriver
 * sc stop FirstDriver
 * 
 * 打开测试签名模式
 * bcdedit /set testsigning on
 * 
 * 
 * HKLM\SYSTEM\CurrentControlSet\Control\Session Manager下新增 Debug Print Filter的键
 * 增加一个叫作DEFAULT的DWORD值（不是键本身的那个默认值）并置其值为8
 */
extern "C" NTSTATUS DriverEntry(PDRIVER_OBJECT driver, PUNICODE_STRING reg)
{
	//编译器被默认设置成将警告作为错误对待
	//通过UNREFERENCED_PARAMETER消除警告
	UP(driver);
	UP(reg);

	//驱动程序可以拥有一个Unload例程，它会在驱动程序从内存中卸载时自动得到调用。
	driver->DriverUnload = DriverUnload;

	KdPrint(("driver install success \n"));
	
	return STATUS_SUCCESS;
}