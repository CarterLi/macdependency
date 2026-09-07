#ifndef DYLD_CACHE_IMAGE_H
#define DYLD_CACHE_IMAGE_H

#include "macho_global.h"

#include <string>
#include <vector>

/**
 * A library that has no representation on disk (it only lives inside the dyld
 * shared cache) can still be inspected: dyld maps it into this process as soon
 * as it is dlopen()ed, and the mapped image carries the very same load commands
 * and LINKEDIT data that an on-disk Mach-O file would have.
 *
 * This class dlopen()s such a library, then copies the pieces MacDependency
 * needs -- header, load commands, symbol table and string table -- into one
 * contiguous buffer and rewrites the LINKEDIT offsets so that the result is a
 * self contained Mach-O file image which can be parsed by the regular
 * MachOFile / MachOArchitecture code.
 *
 * Exports are taken from LC_DYLD_EXPORTS_TRIE (which is authoritative -- the
 * symbol table of some cached images, AppKit for instance, marks far fewer
 * symbols as external than the image actually exports). Imports are the
 * undefined entries of LC_SYMTAB.
 */
class EXPORT DyldCacheImage
{
public:
    DyldCacheImage();
    ~DyldCacheImage();

    /** Maps `path` and builds the in-memory file image. Throws MachOException. */
    void load(const std::string& path);

    /** Owning buffer with the synthesized Mach-O file image. */
    const char* getData() const { return buffer.empty() ? 0 : &buffer[0]; }
    unsigned long long getSize() const { return (unsigned long long)buffer.size(); }

private:
    std::vector<char> buffer;
};

#endif // DYLD_CACHE_IMAGE_H
