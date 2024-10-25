#pragma once
#include <ntifs.h>

enum class ItemType
{
	None,
	ProcessCreate,
	ProcessExit,
	ThreadCreate,
	ThreadExit,
	ImageLoad
};

//ͨ����Ϣͷ
struct ItemHeader
{
	ItemType type; //����
	USHORT size; //�ṹ��С
	LARGE_INTEGER time; //ϵͳʱ��
};

//�����˳�
struct ProcessExitInfo : ItemHeader
{
	ULONG pid;
};

//���̴���
struct ProcessCreateInfo : ItemHeader
{
	ULONG pid;
	ULONG ppid;
	USHORT cmdLen;
	USHORT cmdOffset;
	USHORT imgLen;
	USHORT imgOffset;
};

//�̴߳����˳���Ϣ
struct ThreadCreateExitInfo : ItemHeader
{
	ULONG tid;
	ULONG pid;
};

//ģ�������Ϣ
struct ImageLoadInfo : ItemHeader
{
	ULONG pid;
	ULONG_PTR imageBase;
	ULONG_PTR imageSize;
	USHORT pathLen;
	USHORT pathOffset;
};

template<typename T>
struct FullItem
{
	LIST_ENTRY entry;
	T data;
};

struct FastMutex 
{
	void Init()
	{
		ExInitializeFastMutex(&_mutex);
	}
	void Lock()
	{
		ExAcquireFastMutex(&_mutex);
	}
	void Unlock()
	{
		ExReleaseFastMutex(&_mutex);
	}
private:
	FAST_MUTEX _mutex;
};

struct Mutex
{
	void Init()
	{
		KeInitializeMutex(&_mutex, 0);
	}
	void Lock()
	{
		KeWaitForSingleObject(&_mutex, Executive, KernelMode, FALSE, NULL);
	}
	void Unlock()
	{
		KeReleaseMutex(&_mutex, FALSE);
	}
private:
	KMUTEX _mutex;
};

template<typename TLock>
struct AutoLock
{
	AutoLock(TLock& lock) : _lock(lock)
	{
		_lock.Lock();
	}
	~AutoLock()
	{
		_lock.Unlock();
	}
private:
	TLock& _lock;
};

struct Globals
{
	LIST_ENTRY itemsHead;
	ULONG itemCount;
	FastMutex mutex;
};