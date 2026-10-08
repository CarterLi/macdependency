#include "segmentcommand.h"
#include "machoheader.h"
#include "machofile.h"

#include <mach-o/loader.h>

#include <string.h>

namespace {

/** Segment and section names are fixed size and not necessarily terminated. */
std::string fixedString(const char* data, size_t size) {
    size_t length = 0;
    while (length < size && data[length] != '\0')
        length++;
    return std::string(data, length);
}

} // namespace

Section::Section() : address(0), size(0), offset(0) {
}

Section::Section(const char* segmentName, const char* sectionName, uint64_t address, uint64_t size, uint32_t offset) :
    segmentName(fixedString(segmentName, 16)), name(fixedString(sectionName, 16)),
    address(address), size(size), offset(offset)
{
}

SegmentCommand::SegmentCommand(MachOHeader* header) :
    LoadCommand(header), vmAddress(0), vmSize(0), fileOffset(0), fileSize(0), commandSize(0)
{
    // The file is positioned on the command itself, so the whole structure can
    // be read in one go.
    uint32_t cmd = file.readUint32();
    if (cmd == LC_SEGMENT_64) {
        struct segment_command_64 segment;
        file.seek(offset);
        file.readBytes((char*)&segment, sizeof(segment));
        name = fixedString(segment.segname, sizeof(segment.segname));
        vmAddress = file.getUint64(segment.vmaddr);
        vmSize = file.getUint64(segment.vmsize);
        fileOffset = file.getUint64(segment.fileoff);
        fileSize = file.getUint64(segment.filesize);
        commandSize = file.getUint32(segment.cmdsize);
        readSections(file.getUint32(segment.nsects), true);
    } else {
        struct segment_command segment;
        file.seek(offset);
        file.readBytes((char*)&segment, sizeof(segment));
        name = fixedString(segment.segname, sizeof(segment.segname));
        vmAddress = file.getUint32(segment.vmaddr);
        vmSize = file.getUint32(segment.vmsize);
        fileOffset = file.getUint32(segment.fileoff);
        fileSize = file.getUint32(segment.filesize);
        commandSize = file.getUint32(segment.cmdsize);
        readSections(file.getUint32(segment.nsects), false);
    }
}

void SegmentCommand::readSections(unsigned int numberOfSections, bool is64Bit) {
    unsigned int structureSize = is64Bit ? sizeof(struct segment_command_64) : sizeof(struct segment_command);
    unsigned int sectionSize = is64Bit ? sizeof(struct section_64) : sizeof(struct section);

    // A malformed command could claim an absurd number of sections; the
    // remaining bytes of the command are the only ones that can hold them.
    if (commandSize > structureSize) {
        unsigned int available = (commandSize - structureSize) / sectionSize;
        if (numberOfSections > available)
            numberOfSections = available;
    } else {
        numberOfSections = 0;
    }

    for (unsigned int n = 0; n < numberOfSections; n++) {
        if (is64Bit) {
            struct section_64 section;
            file.readBytes((char*)&section, sizeof(section));
            sections.push_back(Section(section.segname, section.sectname,
                                       file.getUint64(section.addr), file.getUint64(section.size),
                                       file.getUint32(section.offset)));
        } else {
            struct section section;
            file.readBytes((char*)&section, sizeof(section));
            sections.push_back(Section(section.segname, section.sectname,
                                       file.getUint32(section.addr), file.getUint32(section.size),
                                       file.getUint32(section.offset)));
        }
    }
}

bool SegmentCommand::containsAddress(uint64_t address) const {
    // Only the part that is backed by file bytes can be read; a segment may be
    // larger in memory than in the file (__PAGEZERO, zero filled tails).
    return fileSize > 0 && address >= vmAddress && address < vmAddress + fileSize;
}

long long SegmentCommand::getFileOffsetForAddress(uint64_t address) const {
    if (!containsAddress(address))
        return -1;
    return (long long)(fileOffset + (address - vmAddress));
}

const Section* SegmentCommand::findSection(const std::string& segmentName, const std::string& sectionName) const {
    for (unsigned int n = 0; n < sections.size(); n++) {
        if (sections[n].getName() == sectionName && sections[n].getSegmentName() == segmentName)
            return &sections[n];
    }
    return 0;
}

const Section* SegmentCommand::findSection(const std::string& sectionName) const {
    for (unsigned int n = 0; n < sections.size(); n++) {
        if (sections[n].getName() == sectionName)
            return &sections[n];
    }
    return 0;
}
