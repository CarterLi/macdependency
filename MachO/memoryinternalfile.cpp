#include "memoryinternalfile.h"

#include <cstring>
#include <dlfcn.h>
#include <sys/stat.h>

namespace {

typedef const char* (*SharedCachePathFunction)(void);

/**
 * Where dyld keeps the shared cache, as dyld itself says.
 *
 * The image of a cached library is not a file, but the cache it was copied out
 * of is, and that is the file the bytes come from. The function that names it
 * belongs to dyld and is looked up rather than linked against, so that a system
 * without it means only that no time can be told, and not that the library
 * cannot be loaded at all.
 */
const char* sharedCachePath() {
  static SharedCachePathFunction function = (SharedCachePathFunction)dlsym(RTLD_DEFAULT, "dyld_shared_cache_file_path");
  return function == 0 ? 0 : function();
}

/** When the shared cache was last written, or 0 when that cannot be told. */
time_t sharedCacheModificationTime() {
  const char* path = sharedCachePath();
  if (path == 0)
    return 0;

  struct stat status;
  if (stat(path, &status) != 0)
    return 0;
  return status.st_mtime;
}

} // namespace

MemoryInternalFile::MemoryInternalFile(const std::string& filename, const char* data, size_t size,
                                       const uint8_t* mappedBase, intptr_t mappedSlide) :
InternalFile(filename), data(data, data + size), position(0), mappedBase(mappedBase), mappedSlide(mappedSlide)
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

// A cached library has no file of its own, so the time is the one of the file
// it does come from: the shared cache dyld serves it out of. Telling the two
// apart is what the File Kind column is for; the date is better off saying
// something true about the image than saying 1970.
time_t MemoryInternalFile::getLastModificationTime() const {
  static const time_t time = sharedCacheModificationTime();
  return time;
}

bool MemoryInternalFile::isInMemory() const {
  return true;
}

// The image is not a file, but the cache it was copied out of is, and that is
// what a person opening or revealing this image should be shown.
std::string MemoryInternalFile::getPath() const {
  const char* path = sharedCachePath();
  return path == 0 ? std::string() : std::string(path);
}
