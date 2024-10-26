#pragma once
#include <ntifs.h>

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

enum class Type
{
	TYPE_ADD,
	TYPE_DEL
};

typedef struct _Package
{
	Type type;
	ULONG pid;
} Package, * PPackage;

typedef struct _ThreadMonItem
{
	LIST_ENTRY entry;
	ULONG pid;
} ThreadMonItem, * PThreadMonItem;

struct ThreadMonList
{
	LIST_ENTRY head;
	FastMutex mutex;
};