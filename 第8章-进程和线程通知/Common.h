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

//通用信息头
struct ItemHeader
{
	ItemType type; //类型
	USHORT size; //结构大小
	LARGE_INTEGER time; //系统时间
};

//进程退出
struct ProcessExitInfo : ItemHeader
{
	ULONG pid;
};

//进程创建
struct ProcessCreateInfo : ItemHeader
{
	ULONG pid;
	ULONG ppid;
	USHORT cmdLen;
	USHORT cmdOffset;
	USHORT imgLen;
	USHORT imgOffset;
};

//线程创建退出信息
struct ThreadCreateExitInfo : ItemHeader
{
	ULONG tid;
	ULONG pid;
};

//模块加载信息
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