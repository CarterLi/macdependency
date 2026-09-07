#ifndef DISKINTERNALFILE_H
#define DISKINTERNALFILE_H

#include "internalfile.h"

#include <fstream>

/** An InternalFile backed by a real file on disk. */
class DiskInternalFile : public InternalFile
{
public:
  explicit DiskInternalFile(const std::string& filename);
  virtual ~DiskInternalFile();

  virtual unsigned long long getSize() const;
  virtual bool seek(long long int position);
  virtual std::streamsize read(char* buffer, std::streamsize size);
  virtual long long int getPosition();
  virtual time_t getLastModificationTime() const;

private:
  std::ifstream file;
  unsigned long long fileSize;
  time_t lastWriteTime;
};

#endif // DISKINTERNALFILE_H
