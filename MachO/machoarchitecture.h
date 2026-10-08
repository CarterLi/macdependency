#ifndef MACHOARCHITECTURE_H
#define MACHOARCHITECTURE_H

#include "macho_global.h"

#include <string>
#include <vector>

class MachOFile;
class MachOHeader;
class LoadCommand;
class DylibCommand;
class SegmentCommand;

class EXPORT MachOArchitecture
{
private:
  typedef std::list<LoadCommand*> LoadCommands;
  typedef LoadCommands::iterator LoadCommandsIterator;
public:
  typedef LoadCommands::const_iterator LoadCommandsConstIterator;

  MachOArchitecture(MachOFile& file, uint32_t magic, unsigned int size);
  ~MachOArchitecture();

  const MachOHeader* getHeader() const { return header; }
  LoadCommandsConstIterator getLoadCommandsBegin() const { if(!hasReadLoadCommands) { readLoadCommands(); } return loadCommands.begin(); }
  LoadCommandsConstIterator getLoadCommandsEnd() const { if(!hasReadLoadCommands) { readLoadCommands(); } return loadCommands.end(); }
  DylibCommand* getDynamicLibIdCommand() const { if(!hasReadLoadCommands) { readLoadCommands(); } return dynamicLibIdCommand; }
  unsigned int getSize() const;
  void initParentArchitecture(const MachOArchitecture* parent);
  const MachOFile* getFile() const { return &file; }
  std::string getDynamicLinker() const { return dylinker; }
  std::vector<std::string*> getRpaths(bool recursively = true) const;
  std::string getResolvedName(const std::string& name, const std::string& workingPath) const;
  const uint8_t* getUuid() const;

  /** All LC_SEGMENT / LC_SEGMENT_64 commands of this architecture. */
  std::vector<SegmentCommand*> getSegments() const;

  /** Address the image itself is mapped to, i.e. the address of __TEXT. */
  uint64_t getImageBase() const;

  /** Size of the image as dyld mapped it. Only meaningful for a mapped image. */
  uint64_t getMappedSize() const;

  /**
   * Offset (relative to the beginning of the Mach-O image) of the bytes that
   * are mapped at `vmAddress`, or -1 when the address is not backed by the file.
   */
  long long getFileOffset(uint64_t vmAddress) const;

  /** Reads `size` bytes at an offset relative to the beginning of the image. */
  bool readAtFileOffset(uint64_t offset, void* buffer, size_t size) const;

  /** Reads `size` bytes from `vmAddress`. Returns false when they are not there. */
  bool readAtAddress(uint64_t vmAddress, void* buffer, size_t size) const;

  /**
   * Reads `size` bytes from an address in the address space of this process.
   *
   * readAtAddress translates the address through the image, which is what the
   * load commands use; pointers that were taken out of a mapped image however
   * are process addresses already, and some of them -- the class data the
   * Objective-C runtime builds for a realized class, for instance -- do not
   * belong to the image at all. Those are read here. Returns false when the
   * memory is not readable instead of crashing.
   */
  bool readFromProcess(uint64_t address, void* buffer, size_t size) const;

private:
  MachOHeader* header;
  MachOFile& file;
  const unsigned int size;
  mutable bool hasReadLoadCommands;
  void readLoadCommands() const;
  const MachOArchitecture* parent;	// architecture from which this architecture was loaded

  // all those are mutable, because they are initialized not in the constructor, but in the readLoadCommands method
  mutable LoadCommands loadCommands;
  mutable DylibCommand* dynamicLibIdCommand;
  mutable std::vector<std::string*> rpaths;
  mutable const uint8_t* uuid;
  mutable std::string dylinker;
};

#endif // MACHOARCHITECTURE_H
