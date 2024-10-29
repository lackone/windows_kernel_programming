#include "DeviceMonManager.h"

void DeviceMonManager::Init(PDRIVER_OBJECT driver)
{
	_lock.Init();
	_driver = driver;
}

NTSTATUS DeviceMonManager::AddDevice(PCWSTR name)
{
	AutoLock<FastMutex> lock(_lock);

	//看看数组是否已经用完
	if (_monDeviceCount == MaxMonDevices)
	{
		return STATUS_TOO_MANY_NAMES;
	}

	//已经被过滤了
	if (FindDevice(name) >= 0)
	{
		return STATUS_SUCCESS;
	}

	for (int i = 0; i < MaxMonDevices; i++)
	{
		if (_devices[i].filterObj == nullptr)
		{
			UNICODE_STRING devName = { 0 };
			RtlInitUnicodeString(&devName, name);

			//用IoGetDeviceObjectPointer来获得想要加以过滤的设备对象的指针
			PFILE_OBJECT fileObj = nullptr;
			PDEVICE_OBJECT devObj = nullptr;
			//IoGetDeviceObjectPointer的返回值事实上是顶端的设备对象，未必是目标设备对象
			//因为任何一种附加操作实际上都是附加到设备栈的顶部的
			//也就是这里的devObj对象可能不是原始设备，但相对于我们的过滤设备来讲，它现在是底部设备
			auto status = IoGetDeviceObjectPointer(&devName, FILE_READ_DATA, &fileObj, &devObj);
			if (!NT_SUCCESS(status))
			{
				return status;
			}

			PDEVICE_OBJECT filterObj = nullptr;
			WCHAR* buf = nullptr;

			do 
			{
				//创建过滤设备
				status = IoCreateDevice(_driver, sizeof(DeviceExt), nullptr, FILE_DEVICE_UNKNOWN, 0, FALSE, &filterObj);

				if (!NT_SUCCESS(status))
				{
					break;
				}

				buf = (WCHAR*)ExAllocatePoolWithTag(PagedPool, devName.Length, 0);
				if (!buf)
				{
					status = STATUS_INSUFFICIENT_RESOURCES;
					break;
				}

				auto ext = (PDeviceExt)filterObj->DeviceExtension;

				filterObj->Flags |= devObj->Flags & (DO_BUFFERED_IO | DO_DIRECT_IO);
				filterObj->DeviceType = devObj->DeviceType;

				_devices[i].devName.Buffer = buf;
				_devices[i].devName.MaximumLength = devName.Length;

				//复制设备名称
				RtlCopyUnicodeString(&_devices[i].devName, &devName);

				_devices[i].filterObj = filterObj;
				_devices[i].devObj = devObj;

				status = IoAttachDeviceToDeviceStackSafe(filterObj, devObj, &ext->lowObj);
				if (!NT_SUCCESS(status))
				{
					break;
				}

				_devices[i].lowObj = ext->lowObj;

				//初始化完成
				filterObj->Flags &= ~DO_DEVICE_INITIALIZING;
				filterObj->Flags |= DO_POWER_PAGABLE;

				_monDeviceCount++;

			} while (0);

			if (!NT_SUCCESS(status))
			{
				if (buf)
				{
					ExFreePoolWithTag(buf, 0);
				}
				if (filterObj)
				{
					IoDeleteDevice(filterObj);
				}
				_devices[i].filterObj = nullptr;
			}

			if (fileObj)
			{
				ObDereferenceObject(fileObj);
			}

			return status;
		}
	}

	return STATUS_UNSUCCESSFUL;
}

int DeviceMonManager::FindDevice(PCWSTR name)
{
	UNICODE_STRING devName = { 0 };
	RtlInitUnicodeString(&devName, name);

	for (int i = 0; i < MaxMonDevices; i++)
	{
		auto& device = _devices[i];
		if (device.filterObj && RtlEqualUnicodeString(&device.devName, &devName, TRUE))
		{
			return i;
		}
	}

	return -1;
}

bool DeviceMonManager::RemoveDevice(PCWSTR name)
{
	AutoLock<FastMutex> lock(_lock);

	int index = FindDevice(name);

	if (index < 0)
	{
		return false;
	}

	return RemoveDevice(index);
}

void DeviceMonManager::RemoveAllDevice()
{
	AutoLock<FastMutex> lock(_lock);

	for (int i = 0; i < MaxMonDevices; i++)
	{
		RemoveDevice(i);
	}
}

MonDevice& DeviceMonManager::GetDevice(int index)
{
	return _devices[index];
}

bool DeviceMonManager::RemoveDevice(int index)
{
	auto& device = _devices[index];
	if (device.filterObj == nullptr)
	{
		return false;
	}

	ExFreePoolWithTag(device.devName.Buffer, 0);
	IoDetachDevice(device.lowObj);
	IoDeleteDevice(device.filterObj);
	device.filterObj = nullptr;

	_monDeviceCount--;

	return true;
}