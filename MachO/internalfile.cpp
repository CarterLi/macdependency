#include "internalfile.h"
#include "diskinternalfile.h"
#include "memoryinternalfile.h"
#include "dyldcacheimage.h"
#include "machoexception.h"

#include <climits>
#include <cstdlib>

// use reference counting to reuse files for all used architectures
InternalFile* InternalFile::create(InternalFile* file) {
  file->counter++;
  return file;
}

InternalFile* InternalFile::create(const std::string& filename) {
  try {
    return new DiskInternalFile(filename);
  } catch (MachOException&) {
    // no readable file on disk -- maybe the library only exists inside the
    // dyld shared cache
  }

  // Throws a MachOException when the library cannot be mapped either.
  DyldCacheImage image;
  image.load(filename);
  return new MemoryInternalFile(filename, image.getData(), image.getSize());
}

void InternalFile::release() {
  counter--;
  if (counter < 1) {
    delete this;
  }
}

InternalFile::InternalFile(const std::string& filename) :
counter(1), filename(filename)
{
}

InternalFile::~InternalFile()
{
}

/* returns whole filename (including path)*/
std::string InternalFile::getName() const {
	// Try to canonicalize path.
  char buffer[PATH_MAX];
	if (realpath(filename.c_str(), buffer) == nullptr)
    return filename;

	return buffer;
}

/* returns filename without path */
std::string InternalFile::getTitle() const {
  return filename;
}
