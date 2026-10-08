#ifndef SYMBOLTABLEENTRY_H
#define SYMBOLTABLEENTRY_H

#include "macho_global.h"
#include <mach-o/loader.h>
#include <mach-o/nlist.h>
class MachOFile;

class EXPORT SymbolTableEntry
{
public:
  SymbolTableEntry(MachOFile& file, char* stringTable);
  virtual ~SymbolTableEntry();

  /**
   * The name of the symbol, demangled when asked for.
   *
   * Demangling is what makes a name readable: the leading underscore of the C
   * naming convention goes, the mangled C++ and Swift names are turned into
   * what they were written as, and the Objective-C metadata symbols lose the
   * prefix that spells out their kind, so that "_OBJC_CLASS_$_NSString" reads
   * "NSString".
   */
  std::string getName(bool shouldDemangle) const;

  enum Type {
    TypeExported = 0,
    TypeImported,
    TypeLocal,
    TypeDebug,
    TypePrivateExtern,
    NumTypes
  };

  Type getType() const;

  /**
   * What the symbol is, as far as its name says.
   *
   * The symbol table records no language, and for most symbols the name is all
   * there is to go on: the prefixes of the two manglings and of the
   * Objective-C metadata are what tell the kinds apart.
   */
  enum Kind {
    KindC = 0,
    KindCXX,
    KindSwift,
    KindObjCClass,
    KindObjCMetaclass,
    KindObjCProtocol,
    KindObjCIvar,
    KindObjCOther,
    NumKinds
  };

  Kind getKind() const;
  virtual unsigned int getInternalType() const = 0;
  virtual const char* getInternalName() const = 0;

  /**
   * The ordinal of the library an imported symbol is bound to, for symbols
   * that use the two level namespace: 1 is the first library of the load
   * commands, 2 the second, and so on. Zero when the symbol is not bound to
   * one library, which is the case for everything the image defines itself.
   */
  virtual unsigned int getLibraryOrdinal() const = 0;

protected:
  MachOFile& file;
  char* stringTable;
};

#endif // SYMBOLTABLEENTRY_H
