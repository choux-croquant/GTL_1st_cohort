/**
 * ArchiveFileWriter.cpp
 * Implementation of file-based archive writer
 */

#include "ArchiveFileWriter.h"

FArchiveFileWriter::FArchiveFileWriter(const FString& FilePath)
{
	bIsSaving = true;
	bIsLoading = false;
	
	// Open file in binary mode
	FileStream.open(*FilePath, std::ios::binary | std::ios::out);
}

FArchiveFileWriter::~FArchiveFileWriter()
{
	if (FileStream.is_open())
	{
		FileStream.close();
	}
}

void FArchiveFileWriter::SaveData(const void* Data, uint64 Length)
{
	if (FileStream.is_open() && Data && Length > 0)
	{
		FileStream.write(static_cast<const char*>(Data), Length);
	}
}

void FArchiveFileWriter::Seek(int64 InPos)
{
	if (FileStream.is_open())
	{
		FileStream.seekp(InPos, std::ios::beg);
	}
}

int64 FArchiveFileWriter::Tell()
{
	if (FileStream.is_open())
	{
		return static_cast<int64>(FileStream.tellp());
	}
	return -1;
}
