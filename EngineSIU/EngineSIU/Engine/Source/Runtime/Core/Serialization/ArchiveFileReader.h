/**
 * ArchiveFileReader.h
 * File-based archive for reading binary data
 */

#pragma once

#include "Archive.h"
#include <fstream>

/**
 * FArchiveFileReader - Reads binary data from a file
 */
class FArchiveFileReader : public FArchive
{
public:
	explicit FArchiveFileReader(const FString& FilePath);
	virtual ~FArchiveFileReader();

	// Check if file was opened successfully
	bool IsValid() const { return FileStream.is_open(); }

	// FArchive interface
	virtual void LoadData(void* Data, uint64 Length) override;
	virtual void Seek(int64 InPos) override;
	virtual int64 Tell() override;

private:
	std::ifstream FileStream;
};
