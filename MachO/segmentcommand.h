#ifndef SEGMENTCOMMAND_H
#define SEGMENTCOMMAND_H

#include "loadcommand.h"

#include <string>
#include <vector>

/**
 * One section of a segment: where it lives in the address space and where the
 * very same bytes live in the file.
 */
class Section
{
public:
    Section();
    Section(const char* segmentName, const char* sectionName, uint64_t address, uint64_t size, uint32_t offset);

    const std::string& getSegmentName() const { return segmentName; }
    const std::string& getName() const { return name; }
    uint64_t getAddress() const { return address; }
    uint64_t getSize() const { return size; }
    /** Offset relative to the beginning of the Mach-O image. */
    uint32_t getOffset() const { return offset; }
    bool contains(uint64_t vmAddress) const { return vmAddress >= address && vmAddress < address + size; }

private:
    std::string segmentName;
    std::string name;
    uint64_t address;
    uint64_t size;
    uint32_t offset;
};

/**
 * LC_SEGMENT / LC_SEGMENT_64. Keeps the segment range plus the sections it
 * contains, which is everything needed to translate an address into a file
 * offset (and the other way round).
 */
class EXPORT SegmentCommand : public LoadCommand
{
public:
    SegmentCommand(MachOHeader* header);

    virtual unsigned int getSize() const { return commandSize; }

    const std::string& getName() const { return name; }
    uint64_t getVMAddress() const { return vmAddress; }
    uint64_t getVMSize() const { return vmSize; }
    uint64_t getFileOffset() const { return fileOffset; }
    uint64_t getFileSize() const { return fileSize; }

    unsigned int getNumberOfSections() const { return (unsigned int)sections.size(); }
    const Section& getSection(unsigned int index) const { return sections[index]; }

    /** The first section matching both names, or 0. */
    const Section* findSection(const std::string& segmentName, const std::string& sectionName) const;
    /** The first section with that name, in any segment, or 0. */
    const Section* findSection(const std::string& sectionName) const;

    /** True when the address is backed by bytes in the file. */
    bool containsAddress(uint64_t vmAddress) const;

    /** File offset of `vmAddress`, relative to the beginning of the Mach-O image. */
    long long getFileOffsetForAddress(uint64_t vmAddress) const;

private:
    void readSections(unsigned int numberOfSections, bool is64Bit);

    std::string name;
    uint64_t vmAddress;
    uint64_t vmSize;
    uint64_t fileOffset;
    uint64_t fileSize;
    unsigned int commandSize;
    std::vector<Section> sections;
};

#endif // SEGMENTCOMMAND_H
