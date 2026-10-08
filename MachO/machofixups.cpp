#include "machofixups.h"

#include "machoarchitecture.h"
#include "machoheader.h"
#include "loadcommand.h"
#include "linkeditdatacommand.h"
#include "segmentcommand.h"

#include <string.h>

#ifndef LC_DYLD_INFO
#define LC_DYLD_INFO 0x22
#endif
#ifndef LC_DYLD_INFO_ONLY
#define LC_DYLD_INFO_ONLY 0x80000022
#endif
#ifndef LC_DYLD_CHAINED_FIXUPS
#define LC_DYLD_CHAINED_FIXUPS 0x80000034
#endif

namespace {

// dyld_chained_fixups pointer formats
const uint16_t kPtrArm64e = 1;
const uint16_t kPtr64 = 2;
const uint16_t kPtr64Offset = 6;
const uint16_t kPtrArm64eUserland = 9;
const uint16_t kPtrArm64eUserland24 = 12;

// dyld_chained_fixups import formats
const uint32_t kImport = 1;
const uint32_t kImportAddend = 2;
const uint32_t kImportAddend64 = 3;

// dyld_chained_starts_in_segment page markers
const uint16_t kPageStartNone = 0xFFFF;
const uint16_t kPageStartMulti = 0x8000;

// Sizes beyond these are not something a real image would contain.
const uint32_t kMaxFixupsBlobSize = 64u * 1024u * 1024u;
const size_t kMaxFixups = 8u * 1024u * 1024u;
const size_t kMaxChainLength = 1000000u;
const size_t kMaxImports = 4u * 1024u * 1024u;

// LC_DYLD_INFO bind opcodes
const uint8_t kBindOpcodeMask = 0xF0;
const uint8_t kBindImmediateMask = 0x0F;
const uint8_t kBindOpcodeDone = 0x00;
const uint8_t kBindOpcodeSetDylibOrdinalImm = 0x10;
const uint8_t kBindOpcodeSetDylibOrdinalUleb = 0x20;
const uint8_t kBindOpcodeSetDylibSpecialImm = 0x30;
const uint8_t kBindOpcodeSetSymbolTrailingFlagsImm = 0x40;
const uint8_t kBindOpcodeSetTypeImm = 0x50;
const uint8_t kBindOpcodeSetAddendSleb = 0x60;
const uint8_t kBindOpcodeSetSegmentAndOffsetUleb = 0x70;
const uint8_t kBindOpcodeAddAddrUleb = 0x80;
const uint8_t kBindOpcodeDoBind = 0x90;
const uint8_t kBindOpcodeDoBindAddAddrUleb = 0xA0;
const uint8_t kBindOpcodeDoBindAddAddrImmScaled = 0xB0;
const uint8_t kBindOpcodeDoBindUlebTimesSkippingUleb = 0xC0;

uint16_t readUint16(const uint8_t* data) {
    uint16_t value;
    memcpy(&value, data, sizeof(value));
    return value;
}

uint32_t readUint32(const uint8_t* data) {
    uint32_t value;
    memcpy(&value, data, sizeof(value));
    return value;
}

uint64_t readUint64(const uint8_t* data) {
    uint64_t value;
    memcpy(&value, data, sizeof(value));
    return value;
}

uint64_t readUleb128(const uint8_t* data, size_t size, size_t& position) {
    uint64_t result = 0;
    unsigned int shift = 0;
    while (position < size) {
        uint8_t byte = data[position++];
        result |= (uint64_t)(byte & 0x7Fu) << shift;
        if ((byte & 0x80u) == 0)
            break;
        shift += 7;
        if (shift > 63)
            break;
    }
    return result;
}

int64_t readSleb128(const uint8_t* data, size_t size, size_t& position) {
    int64_t result = 0;
    unsigned int shift = 0;
    uint8_t byte = 0;
    while (position < size) {
        byte = data[position++];
        result |= (int64_t)(byte & 0x7Fu) << shift;
        shift += 7;
        if ((byte & 0x80u) == 0)
            break;
        if (shift > 63)
            break;
    }
    if (shift < 64 && (byte & 0x40u) != 0)
        result |= -((int64_t)1 << shift);
    return result;
}

} // namespace

MachOFixups::MachOFixups(const MachOArchitecture& anArchitecture) :
    architecture(anArchitecture), supported(true), hasChainedFixups(false),
    mappedBase(0), mappedSlide(0), mappedSize(0)
{
    mappedBase = architecture.getFile()->getMappedBase();
    mappedSlide = architecture.getFile()->getMappedSlide();
    if (mappedBase != 0) {
        // dyld has already written every pointer of this image, so the fixup
        // descriptions stored on disk no longer describe what is in memory.
        // Reading the pointers as they are is all that is needed.
        mappedSize = architecture.getMappedSize();
        return;
    }

    for (MachOArchitecture::LoadCommandsConstIterator it = architecture.getLoadCommandsBegin();
         it != architecture.getLoadCommandsEnd();
         ++it)
    {
        LinkeditDataCommand* linkedit = dynamic_cast<LinkeditDataCommand*>(*it);
        if (linkedit == 0)
            continue;

        switch (linkedit->getCommand()) {
            case LC_DYLD_CHAINED_FIXUPS:
                hasChainedFixups = true;
                readChainedFixups(linkedit->getDataOffset(), linkedit->getDataSize());
                break;
            case LC_DYLD_INFO:
            case LC_DYLD_INFO_ONLY:
                readDyldInfoBinds(linkedit->getBindOffset(), linkedit->getBindSize());
                readDyldInfoBinds(linkedit->getLazyBindOffset(), linkedit->getLazyBindSize());
                break;
            default:
                break;
        }
    }
}

uint64_t MachOFixups::readPointer(uint64_t vmAddress) const {
    uint64_t value = 0;
    if (!architecture.readAtAddress(vmAddress, &value, sizeof(value)))
        return 0;
    return value;
}

bool MachOFixups::readFileBlob(uint64_t offset, uint32_t size, std::vector<uint8_t>& blob) const {
    blob.clear();
    if (size == 0 || size > kMaxFixupsBlobSize)
        return false;
    blob.resize(size);
    if (!architecture.readAtFileOffset(offset, &blob[0], size)) {
        blob.clear();
        return false;
    }
    return true;
}

bool MachOFixups::isAddressInImage(uint64_t address) const {
    return mappedBase != 0 &&
           address >= (uint64_t)(uintptr_t)mappedBase &&
           address < (uint64_t)(uintptr_t)mappedBase + mappedSize;
}

uint64_t MachOFixups::resolve(uint64_t vmAddress) const {
    if (!supported)
        return 0;

    if (mappedBase != 0) {
        // A pointer of a mapped image is an address in this process already.
        // Bring it back into the unslid space the load commands are in; an
        // address outside the image cannot be resolved from the file.
        uint64_t value = readPointer(vmAddress);
        if (!isAddressInImage(value))
            return 0;
        return (uint64_t)((int64_t)value - (int64_t)mappedSlide);
    }

    std::map<uint64_t, Fixup>::const_iterator found = fixups.find(vmAddress);
    if (found == fixups.end())
        return readPointer(vmAddress);
    if (found->second.isImport)
        return 0;
    return found->second.value;
}

bool MachOFixups::isImport(uint64_t vmAddress) const {
    if (!supported)
        return false;
    if (mappedBase != 0)
        return false;
    std::map<uint64_t, Fixup>::const_iterator found = fixups.find(vmAddress);
    return found != fixups.end() && found->second.isImport;
}

std::string MachOFixups::getImportName(uint64_t vmAddress) const {
    if (!supported || mappedBase != 0)
        return std::string();
    std::map<uint64_t, Fixup>::const_iterator found = fixups.find(vmAddress);
    if (found == fixups.end() || !found->second.isImport)
        return std::string();
    if (found->second.value >= imports.size())
        return std::string();
    return imports[(size_t)found->second.value];
}

bool MachOFixups::isExternal(uint64_t vmAddress) const {
    if (!supported || mappedBase == 0)
        return false;
    uint64_t value = readPointer(vmAddress);
    return value != 0 && !isAddressInImage(value);
}

uint64_t MachOFixups::getExternalAddress(uint64_t vmAddress) const {
    if (!supported || mappedBase == 0)
        return 0;
    uint64_t value = readPointer(vmAddress);
    if (value == 0 || isAddressInImage(value))
        return 0;
    return value;
}

// ---------------------------------------------------------------------------
// Chained fixups
// ---------------------------------------------------------------------------

void MachOFixups::readChainedFixups(uint32_t offset, uint32_t size) {
    std::vector<uint8_t> blob;
    if (!readFileBlob(offset, size, blob))
        return;
    if (blob.size() < 28) {
        supported = false;
        return;
    }

    uint32_t startsOffset = readUint32(&blob[4]);
    uint32_t importsOffset = readUint32(&blob[8]);
    uint32_t symbolsOffset = readUint32(&blob[12]);
    uint32_t importsCount = readUint32(&blob[16]);
    uint32_t importsFormat = readUint32(&blob[20]);
    uint32_t symbolsFormat = readUint32(&blob[24]);

    readImports(blob, importsOffset, symbolsOffset, importsCount, importsFormat, symbolsFormat);

    if (startsOffset + 4 > blob.size()) {
        supported = false;
        return;
    }

    uint32_t segmentCount = readUint32(&blob[startsOffset]);
    std::vector<SegmentCommand*> segments = architecture.getSegments();

    for (uint32_t index = 0; index < segmentCount; index++) {
        size_t entryPosition = (size_t)startsOffset + 4 + (size_t)index * 4;
        if (entryPosition + 4 > blob.size())
            break;
        uint32_t segmentInfoOffset = readUint32(&blob[entryPosition]);
        if (segmentInfoOffset == 0)
            continue;
        if (index >= segments.size())
            continue;

        size_t infoPosition = (size_t)startsOffset + segmentInfoOffset;
        if (infoPosition + 22 > blob.size())
            continue;

        uint16_t pageSize = readUint16(&blob[infoPosition + 4]);
        uint16_t pointerFormat = readUint16(&blob[infoPosition + 6]);
        uint64_t segmentAddress = readUint64(&blob[infoPosition + 8]);
        uint16_t pageCount = readUint16(&blob[infoPosition + 20]);
        if (pageSize == 0)
            continue;

        uint64_t segmentSize = segments[index]->getVMSize();
        if (pageCount == 0 && segmentSize > 0)
            pageCount = (uint16_t)((segmentSize + pageSize - 1) / pageSize);

        for (uint16_t page = 0; page < pageCount; page++) {
            size_t pageEntry = infoPosition + 22 + (size_t)page * 2;
            if (pageEntry + 2 > blob.size())
                break;
            uint16_t pageStart = readUint16(&blob[pageEntry]);
            if (pageStart == kPageStartNone || (pageStart & kPageStartMulti) != 0)
                continue;
            walkChain(segmentAddress, segmentSize, pointerFormat,
                      (uint64_t)page * pageSize + pageStart);
        }
    }
}

void MachOFixups::readImports(const std::vector<uint8_t>& blob, uint32_t importsOffset, uint32_t symbolsOffset,
                              uint32_t importsCount, uint32_t importsFormat, uint32_t symbolsFormat) {
    if (importsCount > kMaxImports) {
        supported = false;
        return;
    }

    size_t importSize = 0;
    switch (importsFormat) {
        case kImport: importSize = 4; break;
        case kImportAddend: importSize = 8; break;
        case kImportAddend64: importSize = 16; break;
        default:
            supported = false;
            return;
    }

    if ((size_t)importsOffset + (size_t)importsCount * importSize > blob.size()) {
        supported = false;
        return;
    }

    // Compressed symbol tables (symbolsFormat == 1) are rare; the bindings
    // themselves stay usable, only their names are missing.
    bool namesUsable = (symbolsFormat == 0) && (symbolsOffset < blob.size());

    imports.reserve(importsCount);
    for (uint32_t index = 0; index < importsCount; index++) {
        const uint8_t* entry = &blob[(size_t)importsOffset + (size_t)index * importSize];
        uint32_t nameOffset = 0;
        if (importsFormat == kImport || importsFormat == kImportAddend)
            nameOffset = readUint32(entry) >> 9;
        else
            nameOffset = (uint32_t)(readUint64(entry) >> 32);

        std::string name;
        if (namesUsable) {
            size_t position = (size_t)symbolsOffset + nameOffset;
            while (position < blob.size() && blob[position] != 0)
                name.push_back((char)blob[position++]);
        }
        imports.push_back(name);
    }
}

void MachOFixups::walkChain(uint64_t segmentAddress, uint64_t segmentSize,
                            uint16_t pointerFormat, uint64_t offsetInSegment) {
    bool format64 = (pointerFormat == kPtr64 || pointerFormat == kPtr64Offset);
    bool formatArm64e = (pointerFormat == kPtrArm64e || pointerFormat == kPtrArm64eUserland ||
                         pointerFormat == kPtrArm64eUserland24);
    if (!format64 && !formatArm64e) {
        // 32 bit pointer formats and the kernel cache formats are not decoded.
        supported = false;
        return;
    }

    uint64_t imageBase = architecture.getImageBase();
    uint64_t vmAddress = segmentAddress + offsetInSegment;
    uint64_t segmentEnd = segmentAddress + segmentSize;

    for (size_t step = 0; step < kMaxChainLength; step++) {
        if (vmAddress < segmentAddress || vmAddress + 8 > segmentEnd)
            return;

        // The pointer slots themselves live in the segments, not in the fixups
        // blob; only the description of the chain is in the blob.
        uint64_t raw = readPointer(vmAddress);
        uint64_t next = 0;

        Fixup fixup;
        if (format64) {
            bool isBind = (raw >> 63) != 0;
            next = (raw >> 51) & 0xFFF;
            if (isBind) {
                fixup.isImport = true;
                fixup.value = raw & 0xFFFFFF;
            } else {
                uint64_t target = raw & 0xFFFFFFFFF;
                uint64_t high8 = (raw >> 36) & 0xFF;
                if (pointerFormat == kPtr64Offset)
                    target += imageBase;
                fixup.isImport = false;
                fixup.value = target | (high8 << 56);
            }
        } else {
            bool isAuth = (raw >> 63) != 0;
            bool isBind = ((raw >> 62) & 1) != 0;
            next = (raw >> 51) & 0x7FF;
            if (isBind) {
                fixup.isImport = true;
                fixup.value = (pointerFormat == kPtrArm64eUserland24) ? (raw & 0xFFFFFF) : (raw & 0xFFFF);
            } else {
                // An authenticated pointer carries a 32 bit offset, an
                // unauthenticated one a 43 bit target. Both are relative to the
                // image base.
                uint64_t target = isAuth ? (raw & 0xFFFFFFFF) : (raw & 0x7FFFFFFFFFF);
                fixup.isImport = false;
                fixup.value = target + imageBase;
            }
        }
        fixups[vmAddress] = fixup;

        if (fixups.size() > kMaxFixups) {
            supported = false;
            return;
        }

        if (next == 0)
            return;
        vmAddress += next * 4;
    }
}

// ---------------------------------------------------------------------------
// Classic dyld info binds
// ---------------------------------------------------------------------------

void MachOFixups::readDyldInfoBinds(uint32_t offset, uint32_t size) {
    std::vector<uint8_t> blob;
    if (!readFileBlob(offset, size, blob))
        return;
    readBindStream(&blob[0], blob.size());
}

void MachOFixups::readBindStream(const uint8_t* data, size_t size) {
    std::vector<SegmentCommand*> segments = architecture.getSegments();

    size_t position = 0;
    uint64_t segmentOffset = 0;
    std::string symbol;
    size_t segmentIndex = 0;
    bool haveSegment = false;

    while (position < size) {
        uint8_t byte = data[position++];
        uint8_t opcode = byte & kBindOpcodeMask;
        uint8_t immediate = byte & kBindImmediateMask;

        switch (opcode) {
            case kBindOpcodeDone:
                // A lazy bind stream is a sequence of independently decodable
                // chunks, so "done" only resets the current symbol.
                symbol.clear();
                haveSegment = false;
                break;
            case kBindOpcodeSetDylibOrdinalImm:
            case kBindOpcodeSetDylibSpecialImm:
            case kBindOpcodeSetTypeImm:
                break;
            case kBindOpcodeSetDylibOrdinalUleb:
                readUleb128(data, size, position);
                break;
            case kBindOpcodeSetSymbolTrailingFlagsImm: {
                symbol.clear();
                while (position < size && data[position] != 0)
                    symbol.push_back((char)data[position++]);
                if (position < size)
                    position++;
                break;
            }
            case kBindOpcodeSetAddendSleb:
                readSleb128(data, size, position);
                break;
            case kBindOpcodeSetSegmentAndOffsetUleb: {
                segmentIndex = immediate;
                segmentOffset = readUleb128(data, size, position);
                haveSegment = segmentIndex < segments.size();
                break;
            }
            case kBindOpcodeAddAddrUleb:
                segmentOffset += readUleb128(data, size, position);
                break;
            case kBindOpcodeDoBind:
            case kBindOpcodeDoBindAddAddrUleb:
            case kBindOpcodeDoBindAddAddrImmScaled:
            case kBindOpcodeDoBindUlebTimesSkippingUleb: {
                uint64_t times = 1;
                uint64_t skip = 0;
                if (opcode == kBindOpcodeDoBindUlebTimesSkippingUleb) {
                    times = readUleb128(data, size, position);
                    skip = readUleb128(data, size, position);
                }
                for (uint64_t n = 0; n < times; n++) {
                    if (haveSegment && !symbol.empty() && imports.size() < kMaxImports) {
                        Fixup fixup;
                        fixup.isImport = true;
                        fixup.value = imports.size();
                        imports.push_back(symbol);
                        fixups[segments[segmentIndex]->getVMAddress() + segmentOffset] = fixup;
                    }
                    segmentOffset += 8 + skip;
                }
                if (opcode == kBindOpcodeDoBindAddAddrUleb)
                    segmentOffset += readUleb128(data, size, position);
                else if (opcode == kBindOpcodeDoBindAddAddrImmScaled)
                    segmentOffset += (uint64_t)immediate * 8;
                break;
            }
            default:
                // Unknown opcode: the rest of the stream cannot be decoded.
                return;
        }
    }
}
