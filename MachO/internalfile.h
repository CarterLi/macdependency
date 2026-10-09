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

  /**
   * True when the bytes are not backed by a file on disk. This is the case for
   * libraries that only exist inside the dyld shared cache: their image is
   * rebuilt in memory by DyldCacheImage. The UI marks those entries specially,
   * because there is nothing on disk to inspect or reveal in the Finder.
   */
  virtual bool isInMemory() const = 0;

  /**
   * When the bytes were taken from an image that dyld mapped into this
   * process, the address of that image and the slide applied to it. Zero
   * otherwise. The mapped image holds everything the synthesized file image
   * does not, so parts of the image that are not in the file can still be read.
   */
  virtual const uint8_t* getMappedBase() const { return 0; }
  virtual intptr_t getMappedSlide() const { return 0; }

  /**
   * The file the bytes come from, as a path to open or to show in the Finder.
   *
   * A file on disk names itself. An image that only lives in the dyld shared
   * cache has no file of its own, so it names the cache the bytes were copied
   * out of: that is the only file there is to point at, the install name being
   * a path that need not exist on disk at all.
   */
  virtual std::string getPath() const;

protected:
  explicit InternalFile(const std::string& filename);

private:
  unsigned int counter;
  std::string filename;
};

#endif // INTERNALFILE_H
