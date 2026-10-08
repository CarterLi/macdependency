#ifndef MACHOFIXUPS_H
#define MACHOFIXUPS_H

#include "macho_global.h"

#include <stdint.h>
#include <map>
#include <string>
#include <vector>

#include <stddef.h>

class MachOArchitecture;

/**
 * Pointer slots inside __DATA / __DATA_CONST are not filled in on disk: the
 * linker leaves a placeholder and dyld rewrites it when the image is loaded.
 * Reading a class list, an ivar list or a method list therefore means resolving
 * those placeholders first.
 *
 * Two encodings are in use:
 *
 *  - chained fixups (LC_DYLD_CHAINED_FIXUPS, macOS 12 / iOS 13.4 and later).
 *    Each slot carries a small structure that either holds a target address
 *    relative to the image base (a rebase) or an index into the import table
 *    (a bind), plus the distance to the next slot of the chain.
 *  - the classic rebase/bind opcode streams of LC_DYLD_INFO(_ONLY), which only
 *    list the slots that need to be written; a slot holding a bind is zero on
 *    disk.
 *
 * Both are decoded here. Images without either (object files, static archives)
 * simply have plain pointers.
 */
class EXPORT MachOFixups
{
public:
    MachOFixups(const MachOArchitecture& architecture);

    /**
     * False when the image uses a pointer encoding this class does not know.
     * The values returned by the accessors are then not trustworthy.
     */
    bool isSupported() const { return supported; }

    /** True when the image has pointer fixups at all. */
    bool hasFixups() const { return !fixups.empty(); }

    /**
     * Address that the pointer slot at `vmAddress` holds. A slot that is an
     * import has no address and yields 0.
     */
    uint64_t resolve(uint64_t vmAddress) const;

    /** True when the slot at `vmAddress` is an import. */
    bool isImport(uint64_t vmAddress) const;

    /** Symbol name the slot at `vmAddress` is bound to, or an empty string. */
    std::string getImportName(uint64_t vmAddress) const;

    /**
     * True when the slot at `vmAddress` points outside this image. Only ever
     * the case for an image that was mapped into this process, where the
     * pointer is an address of another loaded image.
     */
    bool isExternal(uint64_t vmAddress) const;

    /** That outside address, or 0. See isExternal(). */
    uint64_t getExternalAddress(uint64_t vmAddress) const;

private:
    struct Fixup {
        Fixup() : isImport(false), value(0) {}
        bool isImport;
        // rebase: the target address. import: an index into `imports`.
        uint64_t value;
    };

    uint64_t readPointer(uint64_t vmAddress) const;
    bool readFileBlob(uint64_t offset, uint32_t size, std::vector<uint8_t>& blob) const;
    bool isAddressInImage(uint64_t address) const;

    void readChainedFixups(uint32_t offset, uint32_t size);
    void walkChain(uint64_t segmentAddress, uint64_t segmentSize,
                   uint16_t pointerFormat, uint64_t offsetInSegment);
    void readImports(const std::vector<uint8_t>& blob, uint32_t importsOffset, uint32_t symbolsOffset,
                     uint32_t importsCount, uint32_t importsFormat, uint32_t symbolsFormat);

    void readDyldInfoBinds(uint32_t offset, uint32_t size);
    void readBindStream(const uint8_t* data, size_t size);

    const MachOArchitecture& architecture;
    std::vector<std::string> imports;
    std::map<uint64_t, Fixup> fixups;
    bool supported;
    bool hasChainedFixups;

    // Set when the image was mapped into this process by dyld. Its pointers are
    // already relocated, so there is nothing to decode -- they only have to be
    // translated back into the unslid address space the load commands use.
    const uint8_t* mappedBase;
    intptr_t mappedSlide;
    uint64_t mappedSize;
};

#endif // MACHOFIXUPS_H
