#include <ntifs.h>
#include <ntddk.h>
#include <intrin.h>
#include "Mutex.h"

#define ADD_REG CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define DEL_REG CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define CLEAR_REG CTL_CODE(FILE_DEVICE_UNKNOWN, 0x802, METHOD_NEITHER, FILE_ANY_ACCESS)

#define UP(P) UNREFERENCED_PARAMETER(P)

#define MAX_REG_SANDBOX_NUMS 256

typedef struct _RegItem
{
	WCHAR originalKey[256];
	WCHAR sandboxKey[256];
} RegItem, * PRegItem;

typedef struct _RegSandbox
{
	RegItem regArrs[MAX_REG_SANDBOX_NUMS];
	ULONG regNums;
	FastMutex lock;
	LARGE_INTEGER regCookie;
} RegSandbox, * PRegSandbox;

RegSandbox g_RegSandbox;

VOID DriverUnload(PDRIVER_OBJECT DriverObject)
{
	CmUnRegisterCallback(g_RegSandbox.regCookie);

	UNICODE_STRING linkName = { 0 };
	RtlInitUnicodeString(&linkName, L"\\??\\RegSandbox");
	IoDeleteSymbolicLink(&linkName);

	if (DriverObject->DeviceObject)
	{
		IoDeleteDevice(DriverObject->DeviceObject);
	}

	DbgPrintEx(77, 0, "DriverUnload \r\n");
}

NTSTATUS createClose(
	_In_ struct _DEVICE_OBJECT* DeviceObject,
	_Inout_ struct _IRP* Irp
)
{
	UP(DeviceObject);
	Irp->IoStatus.Status = STATUS_SUCCESS;
	Irp->IoStatus.Information = 0;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	return STATUS_SUCCESS;
}

PRegItem findRegItem(PRegItem package)
{
	for (ULONG i = 0; i < MAX_REG_SANDBOX_NUMS; i++)
	{
		if (wcslen(g_RegSandbox.regArrs[i].originalKey) == 0)
		{
			continue;
		}
		if (_wcsicmp(g_RegSandbox.regArrs[i].originalKey, package->originalKey) == 0)
		{
			return &(g_RegSandbox.regArrs[i]);
		}
	}

	return nullptr;
}

PRegItem getRegItem()
{
	for (ULONG i = 0; i < MAX_REG_SANDBOX_NUMS; i++)
	{
		if (wcslen(g_RegSandbox.regArrs[i].originalKey) == 0)
		{

			return &(g_RegSandbox.regArrs[i]);
		}
	}

	return nullptr;
}

NTSTATUS delRegItem(PRegItem package)
{
	AutoLock<FastMutex> lock(g_RegSandbox.lock);

	RegItem* item = findRegItem(package);

	if (item == nullptr)
	{
		return STATUS_NOT_FOUND;
	}

	memset(item->originalKey, 0, 256 * sizeof(WCHAR));
	memset(item->sandboxKey, 0, 256 * sizeof(WCHAR));

	DbgPrintEx(77, 0, "删除 %ws %ws \n", package->originalKey, package->sandboxKey);

	g_RegSandbox.regNums--;

	return STATUS_SUCCESS;
}

NTSTATUS addRegItem(PRegItem package)
{
	AutoLock<FastMutex> lock(g_RegSandbox.lock);

	RegItem* item = getRegItem();
	if (item == nullptr)
	{
		return STATUS_TOO_MANY_NAMES;
	}

	wcscpy(item->originalKey, package->originalKey);
	wcscpy(item->sandboxKey, package->sandboxKey);


	DbgPrintEx(77, 0, "添加 %ws %ws \n", package->originalKey, package->sandboxKey);

	g_RegSandbox.regNums++;

	return STATUS_SUCCESS;
}

NTSTATUS clearRegItem()
{
	for (ULONG i = 0; i < MAX_REG_SANDBOX_NUMS; i++)
	{
		memset(g_RegSandbox.regArrs[i].originalKey, 0, 256 * sizeof(WCHAR));
		memset(g_RegSandbox.regArrs[i].sandboxKey, 0, 256 * sizeof(WCHAR));
	}

	g_RegSandbox.regNums = 0;

	return STATUS_SUCCESS;
}

NTSTATUS ctl(
	_In_ struct _DEVICE_OBJECT* DeviceObject,
	_Inout_ struct _IRP* Irp
)
{
	UP(DeviceObject);
	auto stack = IoGetCurrentIrpStackLocation(Irp);
	auto code = stack->Parameters.DeviceIoControl.IoControlCode;
	auto information = 0;
	auto status = STATUS_SUCCESS;
	auto buf = Irp->AssociatedIrp.SystemBuffer;

	switch (code)
	{
	case ADD_REG:
	{
		PRegItem package = (PRegItem)buf;

		DbgPrintEx(77, 0, "%ws %ws \n", package->originalKey, package->sandboxKey);

		status = addRegItem(package);

		DbgPrintEx(77, 0, "addRegItem = %x \n", status);

		break;
	}
	case DEL_REG:
	{
		PRegItem package = (PRegItem)buf;

		DbgPrintEx(77, 0, "%ws %ws \n", package->originalKey, package->sandboxKey);

		status = delRegItem(package);

		DbgPrintEx(77, 0, "delRegItem = %x \n", status);

		break;
	}
	case CLEAR_REG:
	{
		status = clearRegItem();

		break;
	}
	}


	Irp->IoStatus.Status = status;
	Irp->IoStatus.Information = information;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	return status;
}

PRegItem findRegItem(PCUNICODE_STRING keyName)
{
	WCHAR* key = nullptr;

	if (keyName)
	{
		key = (WCHAR*)ExAllocatePoolWithTag(PagedPool, keyName->Length + sizeof(WCHAR), 0);
		if (!key)
		{
			return nullptr;
		}
		wcsncpy(key, keyName->Buffer, keyName->Length / sizeof(WCHAR));
		key[keyName->Length / sizeof(WCHAR)] = L'\0';
	}

	if (!key)
	{
		return nullptr;
	}

	for (ULONG i = 0; i < MAX_REG_SANDBOX_NUMS; i++)
	{
		if (wcslen(g_RegSandbox.regArrs[i].originalKey) == 0)
		{
			continue;
		}
		if (_wcsicmp(g_RegSandbox.regArrs[i].originalKey, key) == 0)
		{
			return &(g_RegSandbox.regArrs[i]);
		}
	}

	return nullptr;
}

NTSTATUS sandboxHandle(PCUNICODE_STRING keyName, REG_SET_VALUE_KEY_INFORMATION* info)
{
	DbgPrintEx(77, 0, "sandboxHandle %wZ %wZ \n", keyName, info->ValueName);

	auto status = STATUS_SUCCESS;
	PRegItem item = findRegItem(keyName);
	if (!item)
	{
		return status;
	}

	UNICODE_STRING sandboxKey = { 0 };
	RtlInitUnicodeString(&sandboxKey, item->sandboxKey);
	OBJECT_ATTRIBUTES attr = { 0 };
	InitializeObjectAttributes(&attr, &sandboxKey, OBJ_KERNEL_HANDLE|OBJ_CASE_INSENSITIVE, nullptr, nullptr);

	HANDLE hKey = nullptr;
	ULONG disp = 0;
	status = ZwCreateKey(&hKey, KEY_ALL_ACCESS, &attr, 0, nullptr, REG_OPTION_NON_VOLATILE, &disp);
	if (!NT_SUCCESS(status))
	{
		return status;
	}

	//通过句柄得到注册表对象指针
	PVOID keyObj = nullptr;
	status = ObReferenceObjectByHandle(hKey, GENERIC_ALL, *CmKeyObjectType, KernelMode, &keyObj, nullptr);
	if (!NT_SUCCESS(status))
	{
		return status;
	}

	status = ZwSetValueKey(hKey, info->ValueName, info->TitleIndex, info->Type, info->Data, info->DataSize);
	if (!NT_SUCCESS(status))
	{
		return status;
	}

	DbgPrintEx(77, 0, "进行了沙箱处理 %wZ %wZ \n", keyName, info->ValueName);

	//把原来的注册表对象，替换成沙箱的注册表对象，没用
	//info->Object = keyObj;

	//这个返回值很关键
	//当注册表从驱动程序接收STATUS_CALLBACK_BYPASS时，它只会将STATUS_SUCCESS返回到调用线程，并且不处理操作。 驱动程序会抢占注册表操作，并且必须完全处理它
	return STATUS_CALLBACK_BYPASS;
}

NTSTATUS sandboxQueryHandle(PCUNICODE_STRING keyName, REG_QUERY_VALUE_KEY_INFORMATION* info)
{
	auto status = STATUS_SUCCESS;
	PRegItem item = findRegItem(keyName);
	if (!item)
	{
		return status;
	}

	DbgPrintEx(77, 0, "sandboxQueryHandle %wZ %wZ \n", keyName, info->ValueName);

	UNICODE_STRING sandboxKey = { 0 };
	RtlInitUnicodeString(&sandboxKey, item->sandboxKey);
	OBJECT_ATTRIBUTES attr = { 0 };
	InitializeObjectAttributes(&attr, &sandboxKey, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, nullptr, nullptr);

	HANDLE hKey = nullptr;
	ULONG disp = 0;
	status = ZwCreateKey(&hKey, KEY_ALL_ACCESS, &attr, 0, nullptr, REG_OPTION_NON_VOLATILE, &disp);
	if (!NT_SUCCESS(status))
	{
		return status;
	}

	status = ZwQueryValueKey(hKey, info->ValueName, info->KeyValueInformationClass, info->KeyValueInformation, info->Length, info->ResultLength);
	if (!NT_SUCCESS(status))
	{
		return status;
	}

	//这个返回值很关键
	return STATUS_CALLBACK_BYPASS;
}

NTSTATUS RegNotify(
	_In_ PVOID CallbackContext,
	_In_opt_ PVOID Argument1,
	_In_opt_ PVOID Argument2
)
{
	UP(CallbackContext);

	auto status = STATUS_SUCCESS;

	switch ((REG_NOTIFY_CLASS)(ULONG_PTR)Argument1)
	{
	case RegNtPreCreateKey:
	case RegNtPreOpenKey:
	{
		/*
		PREG_PRE_CREATE_KEY_INFORMATION info = (PREG_PRE_CREATE_KEY_INFORMATION)Argument2;
		if (!info || !info->CompleteName)
		{
			break;
		}

		PRegItem item = findRegItem(info->CompleteName);
		if (!item)
		{
			break;
		}

		DbgPrintEx(77, 0, "RegNtPreCreateKey %wZ \n", info->CompleteName);

		// 修改路径为重定向的目标路径
		if (info->CompleteName->MaximumLength > (wcslen(item->sandboxKey) * sizeof(WCHAR)))
		{
			memset(info->CompleteName->Buffer, 0, info->CompleteName->MaximumLength);
			wcsncpy(info->CompleteName->Buffer, item->sandboxKey, wcslen(item->sandboxKey));
			info->CompleteName->Length = (USHORT)(wcslen(item->sandboxKey) * sizeof(WCHAR));
		}

		DbgPrintEx(77, 0, "RegNtPreCreateKey %wZ \n", info->CompleteName);
		*/

		break;
	}
	case RegNtPreCreateKeyEx:
	case RegNtPreOpenKeyEx:
	{
		
		/*
		PREG_CREATE_KEY_INFORMATION info = (PREG_CREATE_KEY_INFORMATION)Argument2;
		if (!info || !info->CompleteName)
		{
			break;
		}

		PRegItem item = findRegItem(info->CompleteName);
		if (!item)
		{
			break;
		}

		PCUNICODE_STRING keyName = { 0 };
		status = CmCallbackGetKeyObjectIDEx(&g_RegSandbox.regCookie, info->RootObject, nullptr, &keyName, 0);
		if (!NT_SUCCESS(status))
		{
			break;
		}

		DbgPrintEx(77, 0, "RegNtPreCreateKeyEx %wZ \n", info->CompleteName);
		DbgPrintEx(77, 0, "keyName %wZ \n", keyName);

		// 修改路径为重定向的目标路径
		// 这里修改键名，好像并没有什么用
		if (info->CompleteName->MaximumLength > (wcslen(item->sandboxKey) * sizeof(WCHAR)))
		{
			memset(info->CompleteName->Buffer, 0, info->CompleteName->MaximumLength);
			wcsncpy(info->CompleteName->Buffer, item->sandboxKey, wcslen(item->sandboxKey));
			info->CompleteName->Length = (USHORT)(wcslen(item->sandboxKey) * sizeof(WCHAR));
		}
		
		DbgPrintEx(77, 0, "RegNtPreCreateKeyEx %wZ \n", info->CompleteName);
		*/

		break;
	}
	case RegNtPreSetValueKey: //为键设置值条目
	{

		auto info = (REG_SET_VALUE_KEY_INFORMATION*)Argument2;
		if (!info || !info->Object)
		{
			break;
		}

		PCUNICODE_STRING keyName = { 0 };
		status = CmCallbackGetKeyObjectIDEx(&g_RegSandbox.regCookie, info->Object, nullptr, &keyName, 0);
		if (!NT_SUCCESS(status))
		{
			break;
		}

		//进行沙箱处理
		status = sandboxHandle(keyName, info);

		CmCallbackReleaseKeyObjectIDEx(keyName);

		break;
	}
	case RegNtPreQueryValueKey:
	{

		auto info = (REG_QUERY_VALUE_KEY_INFORMATION*)Argument2;
		if (!info || !info->Object)
		{
			break;
		}

		PCUNICODE_STRING keyName = { 0 };
		status = CmCallbackGetKeyObjectIDEx(&g_RegSandbox.regCookie, info->Object, nullptr, &keyName, 0);
		if (!NT_SUCCESS(status))
		{
			break;
		}

		status = sandboxQueryHandle(keyName, info);

		CmCallbackReleaseKeyObjectIDEx(keyName);

		break;
	}
	}

	return status;
}

extern "C" NTSTATUS DriverEntry(PDRIVER_OBJECT driver, PUNICODE_STRING reg)
{
	UP(reg);

	auto status = STATUS_SUCCESS;

	PDEVICE_OBJECT devObj = NULL;
	UNICODE_STRING devName = { 0 };
	UNICODE_STRING linkName = { 0 };
	BOOLEAN linkCreate = FALSE;

	g_RegSandbox.lock.Init();

	RtlInitUnicodeString(&devName, L"\\Device\\RegSandbox");
	RtlInitUnicodeString(&linkName, L"\\??\\RegSandbox");

	do
	{
		status = IoCreateDevice(driver, 0, &devName, FILE_DEVICE_UNKNOWN, 0, FALSE, &devObj);
		if (!NT_SUCCESS(status))
		{
			DbgPrintEx(77, 0, "IoCreateDevice = %x \n", status);
			break;
		}
		devObj->Flags |= DO_BUFFERED_IO;

		status = IoCreateSymbolicLink(&linkName, &devName);
		if (!NT_SUCCESS(status))
		{
			DbgPrintEx(77, 0, "IoCreateSymbolicLink = %x \n", status);
			break;
		}
		linkCreate = TRUE;

		UNICODE_STRING altitude = RTL_CONSTANT_STRING(L"123456.78");
		status = CmRegisterCallbackEx(RegNotify, &altitude, driver, nullptr, &g_RegSandbox.regCookie, nullptr);
		if (!NT_SUCCESS(status))
		{
			DbgPrintEx(77, 0, "CmRegisterCallbackEx = %x \n", status);
			break;
		}

	} while (0);

	if (!NT_SUCCESS(status))
	{
		if (linkCreate)
		{
			IoDeleteSymbolicLink(&linkName);
		}
		if (devObj)
		{
			IoDeleteDevice(devObj);
		}
	}

	driver->MajorFunction[IRP_MJ_CREATE] = createClose;
	driver->MajorFunction[IRP_MJ_CLOSE] = createClose;
	driver->MajorFunction[IRP_MJ_DEVICE_CONTROL] = ctl;

	driver->DriverUnload = DriverUnload;
	return STATUS_SUCCESS;
}