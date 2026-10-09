#ifndef MACHOFILE_H
#define MACHOFILE_H

#include "macho_global.h"

class InternalFile;
class MachOFile
{
public:
  MachOFile(const std::string& filename, const MachOFile* parent, bool reversedByteOrder = false);
  MachOFile(const MachOFile& file, bool reversedByteOrder);
  ~MachOFile();

  uint32_t readUint32();
  uint32_t readUint32LE();
  uint32_t readUint32BE();
  uint64_t readUint64();

  void readBytes(char* result, size_t size);

  uint32_t getUint32(unsigned int data) const {return (reversedByteOrder?reverseByteOrder(data):data);}
  uint16_t getUint16(uint16_t data) const {return (reversedByteOrder?(uint16_t)((data >> 8) | (data << 8)):data);}
  uint64_t getUint64(uint64_t data) const;
  static uint32_t getUint32LE(uint32_t data);
  static uint32_t getUint32BE(uint32_t data);
  std::string getDirectory() const;
  std::string getName() const;
  std::string getTitle() const;

  /**
   * The file the bytes come from, as a path to open or to show in the Finder.
   * For an image served out of the dyld shared cache that is the cache itself,
   * the image having no file of its own.
   */
  std::string getPath() const;
  unsigned long long getSize() const;
  void seek(long long int offset) { position = offset; }
  long long int getPosition() const {  return position; }
  const std::string& getExecutablePath() const { return executablePath; }
  time_t getLastModificationTime() const;

  /** True when the underlying bytes come from the dyld shared cache, not disk. */
  bool isInMemory() const;

  /**
   * The image as dyld mapped it into this process, when it came from the dyld
   * shared cache. Zero for a file on disk.
   */
  const uint8_t* getMappedBase() const;
  intptr_t getMappedSlide() const;

private:
  static unsigned int convertByteOrder(char* data, bool isBigEndian, unsigned int numberOfBytes);
  static unsigned int reverseByteOrder(unsigned int data);

  InternalFile* file;
  long long int position;
  const bool reversedByteOrder;
  const MachOFile* parent;
  std::string executablePath;

};

#endif // MACHOFILE_H
