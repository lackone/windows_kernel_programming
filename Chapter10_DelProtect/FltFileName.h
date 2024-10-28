#pragma once
#include <fltKernel.h>

enum class FileNameOptions
{
	Normalized = FLT_FILE_NAME_NORMALIZED,
	Opened = FLT_FILE_NAME_OPENED,
	Short = FLT_FILE_NAME_SHORT,

	QueryDefault = FLT_FILE_NAME_QUERY_DEFAULT,
	QueryCacheOnly = FLT_FILE_NAME_QUERY_CACHE_ONLY,
	QueryFileSystemOnly = FLT_FILE_NAME_QUERY_FILESYSTEM_ONLY,

	RequestFromCurrentProvider = FLT_FILE_NAME_REQUEST_FROM_CURRENT_PROVIDER,
	DoNotCache = FLT_FILE_NAME_DO_NOT_CACHE,
	AllowQueryOnReparse = FLT_FILE_NAME_ALLOW_QUERY_ON_REPARSE
};

// 定义位运算操作
DEFINE_ENUM_FLAG_OPERATORS(FileNameOptions);

struct FilterFileNameInformation 
{
	FilterFileNameInformation(PFLT_CALLBACK_DATA data, FileNameOptions options = FileNameOptions::QueryDefault | FileNameOptions::Normalized)
	{
		auto status = FltGetFileNameInformation(data, (FLT_FILE_NAME_OPTIONS)options, &_info);
	}

	~FilterFileNameInformation()
	{
		if (_info)
		{
			FltReleaseFileNameInformation(_info);
		}
	}

	operator bool() const 
	{
		return _info != nullptr;
	}

	PFLT_FILE_NAME_INFORMATION Get() const 
	{
		return _info;
	}

	operator PFLT_FILE_NAME_INFORMATION() const 
	{
		return Get();
	}

	PFLT_FILE_NAME_INFORMATION operator->() 
	{
		return _info;
	}

	NTSTATUS Parse()
	{
		return FltParseFileNameInformation(_info);
	}

private:
	PFLT_FILE_NAME_INFORMATION _info;
};