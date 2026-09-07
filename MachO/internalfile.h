#ifndef INTERNALFILE_H
#define INTERNALFILE_H

#include "macho_global.h"

#include <ios>
#include <string>

/**
 * Abstract source of raw bytes for the Mach-O parsers.
 *
 * Normally the bytes come from a file on disk (DiskInternalFile), but libraries
 * that only exist inside the dyld shared cache have no file to open. For those
 * MemoryInternalFile serves a Mach-O image that DyldCacheImage rebuilt from the
 * mapped -- already loaded -- image.
 */
class InternalFile
{

public:
  virtual ~InternalFile();

  static InternalFile* create(InternalFile* file);
  static InternalFile* create(const std::string& filename);
  void release();

  std::string getName() const;
  std::string getTitle() const;

  virtual unsigned long long getSize() const = 0;
  virtual bool seek(long long int position) = 0;
  virtual std::streamsize read(char* buffer, std::streamsize size) = 0;
  virtual long long int getPosition() = 0;
  virtual time_t getLastModificationTime() const = 0;

protected:
  explicit InternalFile(const std::string& filename);

private:
  unsigned int counter;
  std::string filename;
};

#endif // INTERNALFILE_H
