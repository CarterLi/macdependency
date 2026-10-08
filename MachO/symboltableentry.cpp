#include "symboltableentry.h"
#include "macho.h"
#include "machofile.h"

#include <dlfcn.h>
#include <string.h>

namespace {

typedef char* (*SwiftDemangleFunction)(const char* mangledName, size_t mangledNameLength,
                                       char* outputBuffer, size_t* outputBufferSize,
                                       uint32_t flags);

/**
 * The demangler of the Swift runtime, loaded on demand.
 *
 * A name is demangled with it only when a Swift name turns up, so an image
 * without Swift code never pays for the runtime. The host process is asked
 * first, because an application that has Swift in it already has the runtime.
 */
SwiftDemangleFunction loadSwiftDemangle() {
  SwiftDemangleFunction demangle =
      (SwiftDemangleFunction)dlsym(RTLD_DEFAULT, "swift_demangle");
  if (demangle != 0)
    return demangle;

  void* runtime = dlopen("/usr/lib/swift/libswiftCore.dylib", RTLD_LAZY | RTLD_LOCAL);
  if (runtime == 0)
    return 0;
  return (SwiftDemangleFunction)dlsym(runtime, "swift_demangle");
}

SwiftDemangleFunction swiftDemangle() {
  static SwiftDemangleFunction demangle = loadSwiftDemangle();
  return demangle;
}

/** True for the prefixes of the Swift mangling: "$s", "$S" and the older "_T". */
bool isSwiftMangledName(const std::string& name) {
  if (name.size() < 2)
    return false;
  return name.compare(0, 2, "$s") == 0 || name.compare(0, 2, "$S") == 0 ||
         name.compare(0, 2, "_T") == 0;
}

/**
 * The prefixes the Objective-C metadata symbols are named with, and what each
 * one stands for.
 *
 * The names come from the assembler, so they carry the leading underscore of
 * the C naming convention -- and the protocol ones carry two of them, which is
 * how every image measured so far spells them. Both spellings are listed, so
 * that an image spelling them the other way is read the same.
 */
const struct {
  const char* prefix;
  SymbolTableEntry::Kind kind;
} kObjCSymbolPrefixes[] = {
  { "_OBJC_CLASS_$_", SymbolTableEntry::KindObjCClass },
  { "_OBJC_METACLASS_$_", SymbolTableEntry::KindObjCMetaclass },
  { "_OBJC_PROTOCOL_$_", SymbolTableEntry::KindObjCProtocol },
  { "__OBJC_PROTOCOL_$_", SymbolTableEntry::KindObjCProtocol },
  { "_OBJC_IVAR_$_", SymbolTableEntry::KindObjCIvar },
};

const unsigned int kObjCSymbolPrefixCount = sizeof(kObjCSymbolPrefixes) / sizeof(*kObjCSymbolPrefixes);

/** The kind of an Objective-C metadata symbol, or NumKinds for any other name. */
SymbolTableEntry::Kind objcSymbolKind(const std::string& name) {
  for (unsigned int n = 0; n < kObjCSymbolPrefixCount; n++) {
    if (name.compare(0, strlen(kObjCSymbolPrefixes[n].prefix), kObjCSymbolPrefixes[n].prefix) == 0)
      return kObjCSymbolPrefixes[n].kind;
  }

  // Everything else the runtime emits is named "_OBJC_<what>_$_<name>", the
  // exception type information of a class for instance. The tables the
  // compiler keeps inside an object file carry one underscore more.
  if (name.compare(0, 6, "_OBJC_") == 0 || name.compare(0, 7, "__OBJC_") == 0)
    return SymbolTableEntry::KindObjCOther;

  return SymbolTableEntry::NumKinds;
}

/**
 * The class, protocol or ivar name a metadata symbol stands for, or an empty
 * string when the symbol is not one of the metadata symbols.
 */
std::string objcSymbolName(const std::string& name) {
  for (unsigned int n = 0; n < kObjCSymbolPrefixCount; n++) {
    size_t length = strlen(kObjCSymbolPrefixes[n].prefix);
    if (name.compare(0, length, kObjCSymbolPrefixes[n].prefix) == 0)
      return name.substr(length);
  }
  return std::string();
}

/**
 * The readable form of a Swift name, or an empty string.
 *
 * The demangler writes into a buffer of the caller's, and a name with a lot of
 * generics in it needs more room than the first size offered, so a second,
 * larger one is tried before giving up.
 */
std::string demangleSwiftName(const std::string& name) {
  SwiftDemangleFunction demangle = swiftDemangle();
  if (demangle == 0)
    return std::string();

  static const size_t sizes[] = { 4096, 65536 };
  for (unsigned int n = 0; n < sizeof(sizes) / sizeof(*sizes); n++) {
    std::vector<char> buffer(sizes[n]);
    size_t size = buffer.size();
    char* result = demangle(name.c_str(), name.size(), &buffer[0], &size, 0);
    if (result != 0)
      return std::string(result);
  }
  return std::string();
}

} // namespace

SymbolTableEntry::SymbolTableEntry(MachOFile& file, char* stringTable)
: file(file), stringTable(stringTable)
{
}

SymbolTableEntry::~SymbolTableEntry() {

}

std::string SymbolTableEntry::getName(bool shouldDemangle) const {
  const char *name = getInternalName();

  if (!shouldDemangle)
    return name;

  // The metadata symbols of the Objective-C runtime spell out what they name,
  // and the name of the class, protocol or ivar is the part worth reading:
  // "_OBJC_CLASS_$_NSString" reads "NSString".
  std::string objcName = objcSymbolName(name);
  if (!objcName.empty())
    return objcName;

  // A name in the symbol table carries the leading underscore of the C naming
  // convention, and that underscore is not part of the name. What is left is
  // either mangled -- and then a demangler says what it is -- or a C name,
  // which is what it is once the underscore is gone.
  if (name[0] != '_' || name[1] == '\0')
    return name;

  std::string mangled = name + 1;

  // C++ first: the Itanium demangler is the one that ships with the C++
  // library, and it is not confused by anything else.
  int status = 0;
  char *cxxName = abi::__cxa_demangle(mangled.c_str(), nullptr, nullptr, &status);
  if (cxxName != nullptr) {
    std::string result = cxxName;
    free(cxxName);
    return result;
  }

  // Swift names have a demangler of their own, and it is not the C++ one.
  if (isSwiftMangledName(mangled)) {
    std::string swiftName = demangleSwiftName(mangled);
    if (!swiftName.empty())
      return swiftName;
  }

  return mangled;
}

SymbolTableEntry::Kind SymbolTableEntry::getKind() const {
  std::string name = getInternalName();

  Kind objc = objcSymbolKind(name);
  if (objc != NumKinds)
    return objc;

  // Both manglings sit behind the leading underscore of the C naming
  // convention, so it is dropped before they are looked for.
  if (name.size() > 1 && name[0] == '_') {
    std::string mangled = name.substr(1);
    if (mangled.compare(0, 2, "_Z") == 0)
      return KindCXX;
    if (isSwiftMangledName(mangled))
      return KindSwift;
  }

  return KindC;
}

SymbolTableEntry::Type SymbolTableEntry::getType() const {
  unsigned int type = getInternalType();

  if (type & N_STAB) {
    return TypeDebug;
  }
  if (type & N_PEXT) {
    return TypePrivateExtern;
  }
  if (type & N_EXT) {
    if ((type & N_TYPE) == N_UNDF)
      return TypeImported;
    else
      return TypeExported;
  }
  else
    return TypeLocal;
}
