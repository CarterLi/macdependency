#ifndef MEMORYINTERNALFILE_H
#define MEMORYINTERNALFILE_H

#include "internalfile.h"

#include <cstddef>
#include <vector>

/**
 * An InternalFile backed by a buffer in memory. Used for libraries of the dyld
 * shared cache, which have no file on disk but whose image can be copied out of
 * the running process.
 */
class MemoryInternalFile : public InternalFile
{
public:
  MemoryInternalFile(const std::string& filename, const char* data, size_t size,
                     const uint8_t* mappedBase = 0, intptr_t mappedSlide = 0);

  virtual unsigned long long getSize() const;
  virtual bool seek(long long int position);
  virtual std::streamsize read(char* buffer, std::streamsize size);
  virtual long long int getPosition();
  virtual time_t getLastModificationTime() const;

  /** Always true: a cached library has no file on disk. */
  virtual bool isInMemory() const;

  /** The image as dyld mapped it, so the parts missing from the buffer stay readable. */
  virtual const uint8_t* getMappedBase() const { return mappedBase; }
  virtual intptr_t getMappedSlide() const { return mappedSlide; }

private:
  std::vector<char> data;
  long long int position;
  const uint8_t* mappedBase;
  intptr_t mappedSlide;
};

#endif // MEMORYINTERNALFILE_H
