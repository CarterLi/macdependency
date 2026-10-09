#ifndef MACHO_H
#define MACHO_H

#include "macho_global.h"
#include <CoreFoundation/CoreFoundation.h>

class MachOFile;
class MachOArchitecture;
class DynamicLoader;

class EXPORT MachO {
private:
  typedef std::list<MachOArchitecture*> MachOArchitectures;
public:
  MachO(const std::string& fileName, const MachO* parent = 0);
  ~MachO();

  std::string getFileName() const;

  /**
   * The file the bytes of this Mach-O come from, as a path to open or to show
   * in the Finder. For an image that only lives in the dyld shared cache this
   * is the cache itself, since the image has no file of its own. Empty when
   * there is nothing to point at.
   */
  std::string getPath() const;

  typedef MachOArchitectures::iterator MachOArchitecturesIterator;
  typedef MachOArchitectures::const_iterator MachOArchitecturesConstIterator;
  MachOArchitecturesIterator getArchitecturesBegin();
  MachOArchitecturesIterator getArchitecturesEnd();
  MachOArchitecture* getCompatibleArchitecture(MachOArchitecture* destArchitecture) const;
  MachOArchitecture* getHostCompatibleArchitecture() const;
  unsigned long long getSize() const;
  time_t getLastModificationTime() const;
  std::string getVersion() const;
  std::string getName() const;
  bool isInMemory() const;
  const MachO* getParent() { return parent;}
  static DynamicLoader* dynamicLoader;
  static int referenceCounter;
private:
  const MachO* parent;
  MachOFile* file;
  MachOArchitectures architectures;
  CFBundleRef bundle;

  std::string getApplicationInBundle(const std::string& bundlePath);
  static std::string extractStringFromCFStringRef(CFStringRef cfStringRef);
  void init(const std::string& fileName, const MachO* parent);

};

#endif // MACHO_H
