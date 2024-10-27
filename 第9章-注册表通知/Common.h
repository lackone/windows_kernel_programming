#pragma once
#include <ntifs.h>

enum class ItemType
{
	None,
	RegSetValue
};

//通用信息头
struct ItemHeader
{
	ItemType type; //类型
	USHORT size; //结构大小
	LARGE_INTEGER time; //系统时间
};

//注册表设置信息
struct RegSetValueInfo : ItemHeader
{
	ULONG pid;
	ULONG tid;
	WCHAR keyName[256];
	WCHAR valueName[64];
	ULONG dataType;
	UCHAR data[128];
	ULONG dataSize;
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
	LARGE_INTEGER regCookie;
};