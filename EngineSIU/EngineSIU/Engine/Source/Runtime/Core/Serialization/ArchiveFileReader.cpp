/**
 * ArchiveFileReader.cpp
 * Implementation of file-based archive reader
 */

#include "ArchiveFileReader.h"

FArchiveFileReader::FArchiveFileReader(const FString& FilePath)
{
	bIsSaving = false;
	bIsLoading = true;
	
	// Open file in binary mode
	FileStream.open(*FilePath, std::ios::binary | std::ios::in);
}

FArchiveFileReader::~FArchiveFileReader()
{
	if (FileStream.is_open())
	{
		FileStream.close();
	}
}

void FArchiveFileReader::LoadData(void* Data, uint64 Length)
{
	if (FileStream.is_open() && Data && Length > 0)
	{
		FileStream.read(static_cast<char*>(Data), Length);
	}
}

void FArchiveFileReader::Seek(int64 InPos)
{
	if (FileStream.is_open())
	{
		FileStream.seekg(InPos, std::ios::beg);
	}
}

int64 FArchiveFileReader::Tell()
{
	if (FileStream.is_open())
	{
		return static_cast<int64>(FileStream.tellg());
	}
	return -1;
}
