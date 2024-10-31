#include <ntifs.h>
#include <ntddk.h>
#include <intrin.h>
#include "Mutex.h"

#define UP(P) UNREFERENCED_PARAMETER(P)

#define ADD_REG CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define DEL_REG CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define CLEAR_REG CTL_CODE(FILE_DEVICE_UNKNOWN, 0x802, METHOD_NEITHER, FILE_ANY_ACCESS)

#define MAX_REG_PROTECT_NUMS 256

#define R3_MACHINE L"\\HKEY_LOCAL_MACHINE"
#define MACHINE L"\\REGISTRY\\MACHINE"

#define R3_USER L"\\HKEY_USERS"
#define USER L"\\REGISTRY\\USER"

typedef struct _RegItem
{
	WCHAR keyName[256];
	WCHAR valueName[256];
} RegItem, * PRegItem;

typedef struct _RegProtect
{
	RegItem regArrs[MAX_REG_PROTECT_NUMS];
	ULONG regNums;
	FastMutex lock;
	LARGE_INTEGER regCookie;
} RegProtect, * PRegProtect;

RegProtect g_RegProtect;

VOID DriverUnload(PDRIVER_OBJECT DriverObject)
{
	CmUnRegisterCallback(g_RegProtect.regCookie);

	UNICODE_STRING linkName = { 0 };
	RtlInitUnicodeString(&linkName, L"\\??\\RegProtect");
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

RegItem* getRegItem()
{
	for (ULONG i = 0; i < MAX_REG_PROTECT_NUMS; i++)
	{
		if (wcslen(g_RegProtect.regArrs[i].keyName) == 0)
		{

			return &(g_RegProtect.regArrs[i]);
		}
	}

	return nullptr;
}

RegItem* findRegItem(PRegItem package)
{
	for (ULONG i = 0; i < MAX_REG_PROTECT_NUMS; i++)
	{
		if (wcslen(g_RegProtect.regArrs[i].keyName) == 0)
		{
			continue;
		}
		if (wcscmp(g_RegProtect.regArrs[i].keyName, package->keyName) == 0)
		{
			if (wcslen(package->valueName) == 0)
			{
				return &(g_RegProtect.regArrs[i]);
			}
			
			if (wcscmp(g_RegProtect.regArrs[i].valueName, package->valueName) == 0)
			{
				return &(g_RegProtect.regArrs[i]);
			}
		}
	}

	return nullptr;
}

NTSTATUS delRegItem(PRegItem package)
{
	AutoLock<FastMutex> lock(g_RegProtect.lock);

	RegItem* item = findRegItem(package);

	if (item == nullptr)
	{
		return STATUS_NOT_FOUND;
	}

	memset(item->keyName, 0, 256 * sizeof(WCHAR));
	memset(item->valueName, 0, 256 * sizeof(WCHAR));

	DbgPrintEx(77, 0, "删除 %ws %ws \n", package->keyName, package->valueName);

	g_RegProtect.regNums--;

	return STATUS_SUCCESS;
}

NTSTATUS addRegItem(PRegItem package)
{
	AutoLock<FastMutex> lock(g_RegProtect.lock);

	RegItem* item = getRegItem();
	if (item == nullptr)
	{
		return STATUS_TOO_MANY_NAMES;
	}

	wcscpy(item->keyName, package->keyName);
	wcscpy(item->valueName, package->valueName);


	DbgPrintEx(77, 0, "添加 %ws %ws \n", package->keyName, package->valueName);

	g_RegProtect.regNums++;

	return STATUS_SUCCESS;
}

NTSTATUS clearRegItem()
{
	for (ULONG i = 0; i < MAX_REG_PROTECT_NUMS; i++)
	{
		memset(g_RegProtect.regArrs[i].keyName, 0, 256 * sizeof(WCHAR));
		memset(g_RegProtect.regArrs[i].valueName, 0, 256 * sizeof(WCHAR));
	}

	g_RegProtect.regNums = 0;

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

		DbgPrintEx(77, 0, "%ws %ws \n", package->keyName, package->valueName);

		status = addRegItem(package);

		DbgPrintEx(77, 0, "addRegItem = %x \n", status);

		break;
	}
	case DEL_REG:
	{
		PRegItem package = (PRegItem)buf;

		DbgPrintEx(77, 0, "%ws %ws \n", package->keyName, package->valueName);

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

bool allowChange(PCUNICODE_STRING keyName, PUNICODE_STRING valueName)
{
	WCHAR* key = nullptr;
	WCHAR* value = nullptr;

	if (keyName)
	{
		key = (WCHAR*)ExAllocatePoolWithTag(PagedPool, keyName->Length + sizeof(WCHAR), 0);
		if (!key)
		{
			return true;
		}
		wcsncpy(key, keyName->Buffer, keyName->Length / sizeof(WCHAR));
		key[keyName->Length / sizeof(WCHAR)] = L'\0';
	}

	if (valueName)
	{
		value = (WCHAR*)ExAllocatePoolWithTag(PagedPool, valueName->Length + sizeof(WCHAR), 0);
		if (!value)
		{
			if (key)
			{
				ExFreePoolWithTag(key, 0);
			}
			return true;
		}
		wcsncpy(value, valueName->Buffer, valueName->Length / sizeof(WCHAR));
		value[valueName->Length / sizeof(WCHAR)] = L'\0';
	}

	DbgPrintEx(77, 0, "%ws == %ws \n", key, value);

	for (ULONG i = 0; i < MAX_REG_PROTECT_NUMS; i++)
	{
		if (wcslen(g_RegProtect.regArrs[i].keyName) == 0)
		{
			continue;
		}
		if (wcscmp(g_RegProtect.regArrs[i].keyName, key) == 0)
		{
			if (wcslen(g_RegProtect.regArrs[i].valueName) == 0)
			{
				//如果没有设valueName，则只要keyName相同，都不让修改
				ExFreePoolWithTag(key, 0);
				ExFreePoolWithTag(value, 0);
				return false;
			}

			if (wcscmp(g_RegProtect.regArrs[i].valueName, value) == 0)
			{
				ExFreePoolWithTag(key, 0);
				ExFreePoolWithTag(value, 0);
				return false;
			}
		}
	}

	if (key)
	{
		ExFreePoolWithTag(key, 0);
	}
	
	if (value)
	{
		ExFreePoolWithTag(value, 0);
	}

	return true;
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
	case RegNtPreSetValueKey: //为键设置值条目
	{
		auto info = (REG_SET_VALUE_KEY_INFORMATION*)Argument2;
		if (!info || !info->Object)
		{
			break;
		}

		PCUNICODE_STRING keyName = { 0 };
		status = CmCallbackGetKeyObjectIDEx(&g_RegProtect.regCookie, info->Object, nullptr, &keyName, 0);
		if (!NT_SUCCESS(status))
		{
			break;
		}
		
		if (!allowChange(keyName, info->ValueName))
		{
			status = STATUS_ACCESS_DENIED;
			DbgPrintEx(77, 0, "keyName=%wZ ValueName=%wZ 被禁止访问 \n", keyName, info->ValueName);
			CmCallbackReleaseKeyObjectIDEx(keyName);
			break;
		}

		CmCallbackReleaseKeyObjectIDEx(keyName);

		break;
	}
	case RegNtPreDeleteKey: //尝试删除密钥
	{
		auto info = (REG_DELETE_KEY_INFORMATION*)Argument2;
		if (!info || !info->Object)
		{
			break;
		}

		PCUNICODE_STRING keyName = { 0 };
		status = CmCallbackGetKeyObjectIDEx(&g_RegProtect.regCookie, info->Object, nullptr, &keyName, 0);
		if (!NT_SUCCESS(status))
		{
			break;
		}
		
		if (!allowChange(keyName, nullptr))
		{
			status = STATUS_ACCESS_DENIED;
			DbgPrintEx(77, 0, "keyName=%wZ 被禁止访问 \n", keyName);
			CmCallbackReleaseKeyObjectIDEx(keyName);
			break;
		}

		CmCallbackReleaseKeyObjectIDEx(keyName);

		break;
	}
	case RegNtPreDeleteValueKey: //尝试删除键的值条目
	{
		auto info = (REG_DELETE_VALUE_KEY_INFORMATION*)Argument2;
		if (!info || !info->Object)
		{
			break;
		}

		PCUNICODE_STRING keyName = { 0 };

		status = CmCallbackGetKeyObjectIDEx(&g_RegProtect.regCookie, info->Object, nullptr, &keyName, 0);
		if (!NT_SUCCESS(status))
		{
			break;
		}
		
		if (!allowChange(keyName, info->ValueName))
		{
			status = STATUS_ACCESS_DENIED;
			DbgPrintEx(77, 0, "keyName=%wZ ValueName=%wZ 被禁止访问 \n", keyName, info->ValueName);
			CmCallbackReleaseKeyObjectIDEx(keyName);
			break;
		}

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

	g_RegProtect.lock.Init();

	PDEVICE_OBJECT devObj = NULL;
	UNICODE_STRING devName = { 0 };
	UNICODE_STRING linkName = { 0 };
	BOOLEAN linkCreate = FALSE;

	RtlInitUnicodeString(&devName, L"\\Device\\RegProtect");
	RtlInitUnicodeString(&linkName, L"\\??\\RegProtect");

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
		status = CmRegisterCallbackEx(RegNotify, &altitude, driver, nullptr, &g_RegProtect.regCookie, nullptr);
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