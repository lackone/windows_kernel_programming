#pragma once
#include <ntifs.h>
#include "Mutex.h"

//���ӵ�����豸����
const int MaxMonDevices = 512;

//��Ҫ���ӵ��豸
typedef struct _MonDevice
{
	UNICODE_STRING devName; //�豸������
	PDEVICE_OBJECT devObj; //�豸����
	PDEVICE_OBJECT filterObj; //�������豸����
	PDEVICE_OBJECT lowObj; //�����ӵ��²��豸����

} MonDevice, * PMonDevice;

//�豸��չ
typedef struct _DeviceExt
{
	PDEVICE_OBJECT lowObj;
} DeviceExt, * PDeviceExt;

class DeviceMonManager
{
public:
	//��ʼ��
	void Init(PDRIVER_OBJECT driver);
	//����豸
	NTSTATUS AddDevice(PCWSTR name);
	//�����豸
	int FindDevice(PCWSTR name);
	//ɾ���豸
	bool RemoveDevice(PCWSTR name);
	//ɾ�������豸
	void RemoveAllDevice();
	//��ȡ�豸
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