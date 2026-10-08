#include "machoarchitecture.h"
#include "machofile.h"
#include "machoheader.h"
#include "loadcommand.h"
#include "segmentcommand.h"
#include "dylibcommand.h"
#include "machoexception.h"
#include "rpathcommand.h"
#include "uuidcommand.h"
#include "dylinkercommand.h"
#include "macho.h"
#include "dynamicloader.h"

#include <string.h>
#include <mach/mach.h>


MachOArchitecture::MachOArchitecture(MachOFile& file, uint32_t magic, unsigned int size) :
    header(MachOHeader::getHeader(file, magic)), file(header->getFile()), size(size), hasReadLoadCommands(false), parent(0), dynamicLibIdCommand(0), uuid(0)
{
}

void MachOArchitecture::initParentArchitecture(const MachOArchitecture* parent) {
    this->parent = parent;
}

std::string MachOArchitecture::getResolvedName(const std::string& name, const std::string& workingPath) const {
    std::string absoluteFileName = MachO::dynamicLoader->getPathname(name, this, workingPath);
    if (!absoluteFileName.empty())
        return absoluteFileName;
    // return unresolved name if it cannot be resolved to a valid absolute name
    return name;
}

std::vector<std::string*> MachOArchitecture::getRpaths(bool recursively) const {
    // try to get it from the parent (recursively)
	std::vector<std::string*> prevRpaths;
	if (recursively && parent) {
		prevRpaths = parent->getRpaths(recursively);
	} else {
		prevRpaths = std::vector<std::string*>();
	}
    // add own rpaths to the end
    prevRpaths.insert(prevRpaths.end(), rpaths.begin(), rpaths.end());
    return prevRpaths;
}

void MachOArchitecture::readLoadCommands() const {
    // read out number of commands
    unsigned int numberOfCommands = header->getNumberOfLoadCommands();
    // read out command identifiers
    for (unsigned int n=0; n<numberOfCommands; n++) {
        unsigned int commandOffset = file.getPosition();
        unsigned int cmd = file.readUint32();

        file.seek(commandOffset);
        LoadCommand* loadCommand = LoadCommand::getLoadCommand(cmd, header);
        
		// for dylibCommand...
		DylibCommand* dylibCommand = dynamic_cast<DylibCommand*>(loadCommand);
        if (dylibCommand != 0 && dylibCommand->isId()) {
            dynamicLibIdCommand = dylibCommand;
        }

		// for rpath command...
        RpathCommand* rpathCommand = dynamic_cast<RpathCommand*>(loadCommand);
        if (rpathCommand != 0) {
            // try to replace placeholder
            std::string resolvedRpath = MachO::dynamicLoader->replacePlaceholder(rpathCommand->getPath(), this);
            if (resolvedRpath.empty()) {
                resolvedRpath = rpathCommand->getPath();
            }
            rpaths.push_back(new std::string(resolvedRpath));
        }
		
		// for uuid command...
		UuidCommand* uuidCommand = dynamic_cast<UuidCommand*>(loadCommand);
        if (uuidCommand != 0) {
			uuid = uuidCommand->getUuid();
        }
		
		// for dylinker command
		DylinkerCommand* dylinkerCommand = dynamic_cast<DylinkerCommand*>(loadCommand);
        if (dylinkerCommand != 0) {
			dylinker = dylinkerCommand->getName();
        }
		
		loadCommands.push_back(loadCommand);
        file.seek(commandOffset + loadCommand->getSize());
    }
    hasReadLoadCommands = true;
}

MachOArchitecture::~MachOArchitecture() {
    delete header;

    for (LoadCommandsIterator it = loadCommands.begin();
    it != loadCommands.end();
    ++it)
    {
        delete *it;
    }

    for (std::vector<std::string*>::iterator it2 = rpaths.begin();
    it2 != rpaths.end();
    ++it2)
    {
        delete *it2;
    }
}

unsigned int MachOArchitecture::getSize() const {
    return size;
}

const uint8_t* MachOArchitecture::getUuid() const {
	return uuid;
}

std::vector<SegmentCommand*> MachOArchitecture::getSegments() const {
    std::vector<SegmentCommand*> segments;
    for (LoadCommandsConstIterator it = getLoadCommandsBegin(); it != getLoadCommandsEnd(); ++it) {
        SegmentCommand* segment = dynamic_cast<SegmentCommand*>(*it);
        if (segment != 0)
            segments.push_back(segment);
    }
    return segments;
}

uint64_t MachOArchitecture::getImageBase() const {
    for (LoadCommandsConstIterator it = getLoadCommandsBegin(); it != getLoadCommandsEnd(); ++it) {
        SegmentCommand* segment = dynamic_cast<SegmentCommand*>(*it);
        if (segment != 0 && segment->getName() == "__TEXT")
            return segment->getVMAddress();
    }
    return 0;
}

uint64_t MachOArchitecture::getMappedSize() const {
    uint64_t imageBase = getImageBase();
    uint64_t end = imageBase;
    std::vector<SegmentCommand*> segments = getSegments();
    for (unsigned int n = 0; n < segments.size(); n++) {
        uint64_t segmentEnd = segments[n]->getVMAddress() + segments[n]->getVMSize();
        if (segmentEnd > end)
            end = segmentEnd;
    }
    return end - imageBase;
}

long long MachOArchitecture::getFileOffset(uint64_t vmAddress) const {
    std::vector<SegmentCommand*> segments = getSegments();
    for (unsigned int n = 0; n < segments.size(); n++) {
        long long offset = segments[n]->getFileOffsetForAddress(vmAddress);
        if (offset >= 0)
            return offset;
    }
    return -1;
}

bool MachOArchitecture::readAtFileOffset(uint64_t offset, void* buffer, size_t size) const {
    unsigned long long absoluteOffset = (unsigned long long)header->getOffset() + offset;
    if (absoluteOffset + size > file.getSize())
        return false;

    long long savedPosition = file.getPosition();
    bool success = false;
    try {
        file.seek((long long)absoluteOffset);
        file.readBytes((char*)buffer, size);
        success = true;
    } catch (MachOException&) {
        success = false;
    }
    // The caller may be in the middle of walking the load commands.
    file.seek(savedPosition);
    return success;
}

bool MachOArchitecture::readFromProcess(uint64_t address, void* buffer, size_t size) const {
    if (size == 0)
        return true;
    if (address == 0)
        return false;

    // The address is not necessarily backed by anything, so the copy is done
    // by the kernel, which reports an error instead of taking the process down.
    vm_size_t read = 0;
    kern_return_t result = vm_read_overwrite(mach_task_self(), (vm_address_t)address, size,
                                             (vm_address_t)buffer, &read);
    return result == KERN_SUCCESS && read == size;
}

bool MachOArchitecture::readAtAddress(uint64_t vmAddress, void* buffer, size_t size) const {
    const uint8_t* mappedBase = file.getMappedBase();
    if (mappedBase != 0) {
        // The image was mapped into this process by dyld. The synthesized file
        // image only holds the header, the load commands and __LINKEDIT, but
        // everything else -- the Objective-C metadata for instance -- can be
        // read straight out of the mapping. dyld laid the segments out relative
        // to the image base, so the file offsets do not apply here.
        uint64_t imageBase = getImageBase();
        if (vmAddress < imageBase)
            return false;
        uint64_t offset = vmAddress - imageBase;
        uint64_t processAddress = (uint64_t)(uintptr_t)mappedBase + offset;
        if (offset + size <= getMappedSize()) {
            memcpy(buffer, mappedBase + offset, size);
            return true;
        }
        // Beyond the image: class name strings of a shared cache image live in
        // a pool that belongs to no single image, so this is still worth a try.
        return readFromProcess(processAddress, buffer, size);
    }

    long long offset = getFileOffset(vmAddress);
    if (offset < 0)
        return false;
    return readAtFileOffset((uint64_t)offset, buffer, size);
}



