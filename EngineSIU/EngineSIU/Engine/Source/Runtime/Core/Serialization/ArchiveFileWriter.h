/**
 * ArchiveFileWriter.h
 * File-based archive for writing binary data
 */

#pragma once

#include "Archive.h"
#include <fstream>

/**
 * FArchiveFileWriter - Writes binary data to a file
 */
class FArchiveFileWriter : public FArchive
{
public:
	explicit FArchiveFileWriter(const FString& FilePath);
	virtual ~FArchiveFileWriter();

	// Check if file was opened successfully
	bool IsValid() const { return FileStream.is_open(); }

	// FArchive interface
	virtual void SaveData(const void* Data, uint64 Length) override;
	virtual void Seek(int64 InPos) override;
	virtual int64 Tell() override;

private:
	std::ofstream FileStream;
};
