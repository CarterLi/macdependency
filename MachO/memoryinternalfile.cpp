#include "memoryinternalfile.h"

#include <cstring>

MemoryInternalFile::MemoryInternalFile(const std::string& filename, const char* data, size_t size) :
InternalFile(filename), data(data, data + size), position(0)
{
}

unsigned long long MemoryInternalFile::getSize() const {
  return (unsigned long long)data.size();
}

bool MemoryInternalFile::seek(long long int position) {
  if (position < 0 || (unsigned long long)position > data.size()) {
    return false;
  }
  this->position = position;
  return true;
}

std::streamsize MemoryInternalFile::read(char* buffer, std::streamsize size) {
  std::streamsize available = (std::streamsize)data.size() - position;
  if (available <= 0 || size <= 0)
    return 0;

  std::streamsize count = (size < available) ? size : available;
  if (!data.empty())
    memcpy(buffer, &data[(size_t)position], (size_t)count);
  position += count;
  return count;
}

long long int MemoryInternalFile::getPosition() {
  return position;
}

// A cached library has no file on disk, so there is no modification time.
time_t MemoryInternalFile::getLastModificationTime() const {
  return 0;
}

bool MemoryInternalFile::isInMemory() const {
  return true;
}
