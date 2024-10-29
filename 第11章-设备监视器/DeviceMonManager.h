#pragma once
#include <ntifs.h>
#include "Mutex.h"

//监视的最大设备数量
const int MaxMonDevices = 512;

//需要监视的设备
typedef struct _MonDevice
{
	UNICODE_STRING devName; //设备的名称
	PDEVICE_OBJECT devObj; //设备对象
	PDEVICE_OBJECT filterObj; //过滤器设备对象
	PDEVICE_OBJECT lowObj; //被附加的下层设备对象

} MonDevice, * PMonDevice;

//设备扩展
typedef struct _DeviceExt
{
	PDEVICE_OBJECT lowObj;
} DeviceExt, * PDeviceExt;

class DeviceMonManager
{
public:
	//初始化
	void Init(PDRIVER_OBJECT driver);
	//添加设备
	NTSTATUS AddDevice(PCWSTR name);
	//查找设备
	int FindDevice(PCWSTR name);
	//删除设备
	bool RemoveDevice(PCWSTR name);
	//删除所有设备
	void RemoveAllDevice();
	//获取设备
	MonDevice& GetDevice(int index);

	PDEVICE_OBJECT _cdo;
private:
	bool RemoveDevice(int index);

private:
	MonDevice _devices[MaxMonDevices];
	int _monDeviceCount;
	FastMutex _lock;
	PDRIVER_OBJECT _driver;
};