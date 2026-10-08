#include "objcmetadata.h"

#include "machoarchitecture.h"
#include "machoheader.h"
#include "segmentcommand.h"
#include "objctypedecoder.h"

#include <dlfcn.h>
#include <objc/runtime.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {

// class_ro_t flags
const uint32_t kClassFlagMeta = 1 << 0;
const uint32_t kClassFlagRoot = 1 << 1;

// Relative method lists mark themselves with this bit in entsizeAndFlags.
const uint32_t kMethodListRelativeFlag = 0x80000000u;
const uint32_t kEntsizeMask = 0x0000FFFCu;

// class_data_bits_t: the pointer is stored above these bits, which hold the
// runtime's own flags instead.
const uint64_t kFastDataMask = 0x00007ffffffffff8ull;

// class_rw_t::flags
const uint32_t kRwRealized = 1u << 31;

// class_rw_t::ro_or_rw_ext carries this bit when it points at a class_rw_ext_t
// (which the runtime builds for a class that has grown beyond its ro) rather
// than at the class_ro_t itself.
const uint64_t kRwExtFlag = 1;

const size_t kMaxStringLength = 8192;
const size_t kMaxListEntries = 500000;
const size_t kMaxClasses = 500000;

// protocol_t::_classProperties was added to the struct after everything else,
// and a protocol_t only carries the field when its own size field covers it:
// the size sits at +64 and is the size of the struct, so the class properties
// are there exactly when it is at least the offset of the field plus a
// pointer. Measured on the protocols of this machine: the field is at +88 and
// the size of a protocol_t that has it is 96.
const uint32_t kProtocolClassPropertiesOffset = 88;
const uint32_t kProtocolSizeWithClassProperties = kProtocolClassPropertiesOffset + 8;

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

int32_t readInt32(const uint8_t* data) {
    int32_t value;
    memcpy(&value, data, sizeof(value));
    return value;
}

/** "_OBJC_CLASS_$_NSString" -> "NSString" */
std::string stripClassSymbolPrefix(const std::string& symbol) {
    static const char* prefixes[] = {
        "_OBJC_CLASS_$_", "_OBJC_METACLASS_$_", "_OBJC_PROTOCOL_$_"
    };
    for (unsigned int n = 0; n < sizeof(prefixes) / sizeof(*prefixes); n++) {
        size_t length = strlen(prefixes[n]);
        if (symbol.compare(0, length, prefixes[n]) == 0)
            return symbol.substr(length);
    }
    return symbol;
}

/** True when the symbol is the one that stands for a class object. */
bool isClassSymbol(const std::string& symbol) {
    static const char* prefixes[] = { "_OBJC_CLASS_$_", "_OBJC_METACLASS_$_" };
    for (unsigned int n = 0; n < sizeof(prefixes) / sizeof(*prefixes); n++) {
        if (symbol.compare(0, strlen(prefixes[n]), prefixes[n]) == 0)
            return true;
    }
    return false;
}

/**
 * The runtime hands out the module qualified name of a Swift protocol
 * ("AppKit._NSAssertion"), while the image and the other dump tools use the
 * mangled symbol ("_TtP6AppKit12_NSAssertion_"). Rebuilding it keeps the two
 * spellings -- and the two ways of reading an image -- consistent.
 */
std::string swiftProtocolMangledName(const std::string& name) {
    size_t dot = name.find('.');
    if (dot == std::string::npos || dot == 0 || dot + 1 >= name.size())
        return name;

    std::string module = name.substr(0, dot);
    std::string type = name.substr(dot + 1);

    char length[24];
    std::string result = "_TtP";
    snprintf(length, sizeof(length), "%u", (unsigned int)module.size());
    result += length;
    result += module;
    snprintf(length, sizeof(length), "%u", (unsigned int)type.size());
    result += length;
    result += type;
    result += "_";
    return result;
}

bool contains(const std::vector<std::string>& values, const std::string& value) {
    for (unsigned int n = 0; n < values.size(); n++) {
        if (values[n] == value)
            return true;
    }
    return false;
}

/**
 * The compiler emits a .cxx_destruct / .cxx_construct method for every class
 * with a C++ ivar. They are bookkeeping, not part of the API, and the other
 * class dump tools leave them out as well.
 */
bool isCompilerGeneratedMethod(const std::string& selector) {
    return selector == ".cxx_destruct" || selector == ".cxx_construct";
}

/**
 * Names the exported symbol that lives at `address`.
 *
 * This is only used for an image that dyld mapped into this process, where a
 * pointer to another image is an address in the running process -- asking the
 * dynamic linker is then the only way to find out what it points at. An image
 * read from disk gets the name from its own bind information instead.
 */
std::string symbolNameAtAddress(uint64_t address) {
    if (address == 0)
        return std::string();

    // An arm64e pointer may carry pointer authentication bits in its high bits.
    const uint64_t candidates[] = { address, address & 0x0000000FFFFFFFFFull };
    for (unsigned int n = 0; n < sizeof(candidates) / sizeof(*candidates); n++) {
        if (candidates[n] == 0)
            continue;
        Dl_info info;
        if (dladdr((const void*)(uintptr_t)candidates[n], &info) == 0 || info.dli_sname == 0)
            continue;
        // Only accept an exact match: otherwise the address sits inside some
        // unrelated symbol and its name would be misleading.
        if ((uint64_t)(uintptr_t)info.dli_saddr == candidates[n])
            return std::string(info.dli_sname);
    }
    return std::string();
}

/**
 * The class a mapped image's classref points at, or an empty string when the
 * address is not a class object. Only class symbols are accepted, so a pointer
 * that happens to land inside an unrelated function cannot name a class.
 */
std::string classSymbolNameAtAddress(uint64_t address) {
    std::string symbol = symbolNameAtAddress(address);
    if (symbol.empty() || !isClassSymbol(symbol))
        return std::string();
    return stripClassSymbolPrefix(symbol);
}

void appendJoined(std::string& target, const std::vector<std::string>& values, const char* separator) {
    for (unsigned int n = 0; n < values.size(); n++) {
        if (n > 0)
            target += separator;
        target += values[n];
    }
}

/** "1 instance method" / "34 instance methods" */
std::string describeCount(uint32_t count, const char* singular) {
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "%u %s%s", count, singular, count == 1 ? "" : "s");
    return std::string(buffer);
}

} // namespace

struct ObjCMetadata::Method {
    std::string name;
    std::string types;
};

struct ObjCMetadata::Ivar {
    std::string name;
    std::string type;
};

struct ObjCMetadata::Property {
    std::string name;
    std::string attributes;
};

struct ObjCMetadata::Class {
    Class() : superclassKnown(false), isRoot(false) {}

    std::string name;
    std::string superclass;
    bool superclassKnown;
    bool isRoot;
    std::vector<std::string> protocols;
    std::vector<Ivar> ivars;
    std::vector<Property> properties;
    std::vector<Method> instanceMethods;
    std::vector<Method> classMethods;
};

struct ObjCMetadata::Category {
    Category() : instanceMethodCount(0), classMethodCount(0) {}

    std::string name;        // the part in parentheses
    std::string className;
    std::vector<std::string> protocols;
    std::vector<Property> properties;
    std::vector<Method> instanceMethods;
    std::vector<Method> classMethods;

    // What the method lists claim to hold. For a shared cache image the lists
    // themselves cannot be decoded, but their headers can, so at least the
    // number of methods is known.
    uint32_t instanceMethodCount;
    uint32_t classMethodCount;
};

struct ObjCMetadata::Protocol {
    std::string name;
    std::vector<std::string> protocols;
    std::vector<Property> properties;
    // A protocol can declare properties of the class as a whole; they are a
    // list of their own and are always required (there is no field for
    // optional ones, and no image has ever turned out to have one).
    std::vector<Property> classProperties;
    std::vector<Method> requiredInstanceMethods;
    std::vector<Method> optionalInstanceMethods;
    std::vector<Method> requiredClassMethods;
    std::vector<Method> optionalClassMethods;
};

ObjCMetadata::ObjCMetadata(const MachOArchitecture& anArchitecture) :
    architecture(anArchitecture), fixups(anArchitecture), parsed(false)
{
}

ObjCMetadata::~ObjCMetadata() {
    for (unsigned int n = 0; n < classes.size(); n++)
        delete classes[n];
    for (unsigned int n = 0; n < categories.size(); n++)
        delete categories[n];
    for (unsigned int n = 0; n < protocols.size(); n++)
        delete protocols[n];
}

const uint8_t* ObjCMetadata::mappedBase() const {
    return architecture.getFile()->getMappedBase();
}

void ObjCMetadata::parse() const {
    if (parsed)
        return;
    parsed = true;

    // The class_t layout that is decoded here is the 64 bit one.
    if (!architecture.getHeader()->is64Bit())
        return;

    // __objc_classlist lives in __DATA_CONST on modern images and in __DATA on
    // older ones, so all segments are searched.
    const Section* classList = 0;
    const Section* categoryList = 0;
    const Section* protocolList = 0;
    std::vector<SegmentCommand*> segments = architecture.getSegments();
    for (unsigned int n = 0; n < segments.size(); n++) {
        if (classList == 0)
            classList = segments[n]->findSection("__objc_classlist");
        if (categoryList == 0)
            categoryList = segments[n]->findSection("__objc_catlist");
        if (protocolList == 0)
            protocolList = segments[n]->findSection("__objc_protolist");
    }

    if (classList != 0) {
        size_t count = (size_t)(classList->getSize() / 8);
        if (count > kMaxClasses)
            count = kMaxClasses;

        for (size_t n = 0; n < count; n++) {
            uint64_t classAddress = fixups.resolve(classList->getAddress() + n * 8);
            if (classAddress == 0)
                continue;
            Class* info = new Class();
            if (readClass(classAddress, *info))
                classes.push_back(info);
            else
                delete info;
        }
    }

    if (categoryList != 0) {
        size_t count = (size_t)(categoryList->getSize() / 8);
        if (count > kMaxClasses)
            count = kMaxClasses;

        for (size_t n = 0; n < count; n++) {
            uint64_t categoryAddress = fixups.resolve(categoryList->getAddress() + n * 8);
            if (categoryAddress == 0)
                continue;
            Category* info = new Category();
            if (readCategory(categoryAddress, *info))
                categories.push_back(info);
            else
                delete info;
        }
    }

    if (protocolList != 0) {
        size_t count = (size_t)(protocolList->getSize() / 8);
        if (count > kMaxClasses)
            count = kMaxClasses;

        for (size_t n = 0; n < count; n++) {
            uint64_t protocolAddress = fixups.resolve(protocolList->getAddress() + n * 8);
            if (protocolAddress == 0)
                continue;
            Protocol* info = new Protocol();
            // Some images list a protocol more than once; declaring it twice
            // would be wrong, so the first one wins.
            if (readProtocol(protocolAddress, *info) && !hasProtocolNamed(info->name))
                protocols.push_back(info);
            else
                delete info;
        }
    }
}

bool ObjCMetadata::hasProtocolNamed(const std::string& name) const {
    for (unsigned int n = 0; n < protocols.size(); n++) {
        if (protocols[n]->name == name)
            return true;
    }
    return false;
}

std::string ObjCMetadata::readString(uint64_t address) const {
    std::string result;
    if (address == 0)
        return result;

    char chunk[64];
    while (result.size() < kMaxStringLength) {
        size_t remaining = kMaxStringLength - result.size();
        size_t request = remaining < sizeof(chunk) ? remaining : sizeof(chunk);
        size_t got = request;
        // The last chunk may reach past the end of the segment; fall back to a
        // single byte so that a string ending right at the boundary still works.
        if (!architecture.readAtAddress(address + result.size(), chunk, request)) {
            got = 1;
            if (!architecture.readAtAddress(address + result.size(), chunk, 1))
                break;
        }
        for (size_t n = 0; n < got; n++) {
            if (chunk[n] == 0)
                return result;
            result.push_back(chunk[n]);
        }
    }
    return result;
}

uint64_t ObjCMetadata::decodeMappedClassData(uint64_t bits) const {
    // An image dyld mapped into this process does not hold the class_ro_t in
    // class_data_bits_t::bits any more: the runtime realizes its classes and
    // rewrites the field to point at the class_rw_t it built, which is a
    // per-process structure that belongs to no image.
    uint64_t data = bits & kFastDataMask;

    uint32_t flags = 0;
    if (!architecture.readFromProcess(data, &flags, sizeof(flags)) || (flags & kRwRealized) == 0) {
        // Not realized after all, so the field still holds the ro.
        return data & ~(uint64_t)7;
    }

    uint64_t roOrRwExt = 0;
    if (!architecture.readFromProcess(data + 8, &roOrRwExt, sizeof(roOrRwExt)))
        return data & ~(uint64_t)7;

    if ((roOrRwExt & kRwExtFlag) != 0) {
        // class_rw_ext_t, whose first member is the class_ro_t.
        uint64_t extended = roOrRwExt & ~(uint64_t)7;
        uint64_t ro = 0;
        if (!architecture.readFromProcess(extended, &ro, sizeof(ro)))
            return data & ~(uint64_t)7;
        return ro & ~(uint64_t)7;
    }

    return roOrRwExt & ~(uint64_t)7;
}

uint64_t ObjCMetadata::classRoAddress(uint64_t classAddress) const {
    const uint8_t* mappedBase = architecture.getFile()->getMappedBase();
    if (mappedBase == 0) {
        // On disk the field holds the address of the ro itself, behind a fixup.
        return fixups.resolve(classAddress + 32) & ~(uint64_t)7;
    }

    uint64_t bits = 0;
    if (!architecture.readAtAddress(classAddress + 32, &bits, sizeof(bits)))
        return 0;

    // decodeMappedClassData works with process addresses, everything else here
    // with the unslid ones the load commands are in.
    uint64_t ro = decodeMappedClassData(bits);
    if (ro == 0)
        return 0;
    return ro - (uint64_t)architecture.getFile()->getMappedSlide();
}

std::string ObjCMetadata::readClassName(uint64_t classAddress) const {
    uint64_t roAddress = classRoAddress(classAddress);
    if (roAddress == 0)
        return std::string();
    return readString(fixups.resolve(roAddress + 24));
}

uint32_t ObjCMetadata::readListCount(uint64_t listAddress) const {
    if (listAddress == 0)
        return 0;
    uint8_t header[8];
    if (!architecture.readAtAddress(listAddress, header, sizeof(header)))
        return 0;
    uint32_t count = readUint32(header + 4);
    return count > kMaxListEntries ? 0 : count;
}

std::string ObjCMetadata::readClassReferenceName(uint64_t slotAddress) const {
    if (fixups.isImport(slotAddress)) {
        // The class lives in another image and the bind names it.
        return stripClassSymbolPrefix(fixups.getImportName(slotAddress));
    }

    if (mappedBase() != 0) {
        // A mapped image holds a real address here, which may belong to any
        // other loaded image; the dynamic linker knows what lives there.
        uint64_t raw = 0;
        if (architecture.readAtAddress(slotAddress, &raw, sizeof(raw))) {
            std::string name = classSymbolNameAtAddress(raw);
            if (!name.empty())
                return name;
        }
    }

    uint64_t address = fixups.resolve(slotAddress);
    if (address == 0)
        return std::string();
    return readClassName(address);
}

void ObjCMetadata::readMethods(uint64_t listAddress, std::vector<Method>& result) const {
    if (listAddress == 0)
        return;

    uint8_t header[8];
    if (!architecture.readAtAddress(listAddress, header, sizeof(header)))
        return;
    uint32_t entsizeAndFlags = readUint32(header);
    uint32_t count = readUint32(header + 4);
    if (count > kMaxListEntries)
        return;

    uint32_t entrySize = entsizeAndFlags & kEntsizeMask;
    bool isRelative = (entsizeAndFlags & kMethodListRelativeFlag) != 0 || entrySize == 12;
    if (entrySize == 0)
        entrySize = isRelative ? 12 : 24;
    if (entrySize < 12 || entrySize > 64)
        return;

    for (uint32_t n = 0; n < count; n++) {
        uint64_t entryAddress = listAddress + 8 + (uint64_t)n * entrySize;
        Method method;
        if (isRelative) {
            uint8_t entry[12];
            if (!architecture.readAtAddress(entryAddress, entry, sizeof(entry)))
                break;
            int32_t nameOffset = readInt32(entry);
            int32_t typesOffset = readInt32(entry + 4);
            // The name offset points at a selector reference (one more
            // indirection), the type offset straight at the type string. Both
            // are relative to the field they are stored in.
            uint64_t selectorSlot = (uint64_t)((int64_t)entryAddress + nameOffset);
            method.name = readString(fixups.resolve(selectorSlot));
            method.types = readString((uint64_t)((int64_t)entryAddress + 4 + typesOffset));
        } else {
            uint8_t entry[24];
            if (!architecture.readAtAddress(entryAddress, entry, sizeof(entry)))
                break;
            method.name = readString(fixups.resolve(entryAddress));
            method.types = readString(fixups.resolve(entryAddress + 8));
        }
        if (!method.name.empty() && !isCompilerGeneratedMethod(method.name))
            result.push_back(method);
    }
}

void ObjCMetadata::readIvars(uint64_t listAddress, std::vector<Ivar>& result) const {
    if (listAddress == 0)
        return;

    uint8_t header[8];
    if (!architecture.readAtAddress(listAddress, header, sizeof(header)))
        return;
    uint32_t entrySize = readUint32(header) & kEntsizeMask;
    uint32_t count = readUint32(header + 4);
    if (entrySize == 0)
        entrySize = 32;
    if (entrySize < 32 || count > kMaxListEntries)
        return;

    for (uint32_t n = 0; n < count; n++) {
        // ivar_t: offset, name, type, alignment, size
        uint64_t entryAddress = listAddress + 8 + (uint64_t)n * entrySize;
        uint8_t entry[32];
        if (!architecture.readAtAddress(entryAddress, entry, sizeof(entry)))
            break;
        Ivar ivar;
        ivar.name = readString(fixups.resolve(entryAddress + 8));
        ivar.type = readString(fixups.resolve(entryAddress + 16));
        if (!ivar.name.empty())
            result.push_back(ivar);
    }
}

void ObjCMetadata::readProperties(uint64_t listAddress, std::vector<Property>& result) const {
    if (listAddress == 0)
        return;

    uint8_t header[8];
    if (!architecture.readAtAddress(listAddress, header, sizeof(header)))
        return;
    uint32_t entrySize = readUint32(header) & kEntsizeMask;
    uint32_t count = readUint32(header + 4);
    if (entrySize == 0)
        entrySize = 16;
    if (entrySize < 16 || count > kMaxListEntries)
        return;

    for (uint32_t n = 0; n < count; n++) {
        // property_t: name, attributes
        uint64_t entryAddress = listAddress + 8 + (uint64_t)n * entrySize;
        uint8_t entry[16];
        if (!architecture.readAtAddress(entryAddress, entry, sizeof(entry)))
            break;
        Property property;
        property.name = readString(fixups.resolve(entryAddress));
        property.attributes = readString(fixups.resolve(entryAddress + 8));
        if (!property.name.empty())
            result.push_back(property);
    }
}

bool ObjCMetadata::readMethodsFromRuntime(uint64_t processClassAddress, bool metaclass,
                                          std::vector<Method>& result) const {
    if (processClassAddress == 0)
        return false;

    // `Class` and `Method` name the nested structs here, so the runtime's own
    // types have to be spelled out.
    ::Class cls = (::Class)(uintptr_t)processClassAddress;
    ::Class target = cls;
    if (metaclass) {
        // The metaclass holds the class methods. It is looked up by name
        // because object_getClass takes an instance, and a class_t is not one.
        const char* className = class_getName(cls);
        if (className == 0)
            return false;
        target = objc_getMetaClass(className);
    }
    if (target == nil)
        return false;

    unsigned int count = 0;
    ::Method* methods = class_copyMethodList(target, &count);
    if (methods == 0) {
        // A class without methods is not an error, the runtime just has
        // nothing to return.
        return count == 0;
    }

    for (unsigned int n = 0; n < count; n++) {
        const char* name = sel_getName(method_getName(methods[n]));
        if (name == 0 || isCompilerGeneratedMethod(name))
            continue;
        const char* types = method_getTypeEncoding(methods[n]);
        Method method;
        method.name = name;
        if (types != 0)
            method.types = types;
        result.push_back(method);
    }

    free(methods);
    return true;
}

bool ObjCMetadata::readProtocolsFromRuntime(uint64_t processClassAddress,
                                            std::vector<std::string>& result) const {
    if (processClassAddress == 0)
        return false;

    ::Class cls = (::Class)(uintptr_t)processClassAddress;
    unsigned int count = 0;
    ::Protocol* const* protocols = class_copyProtocolList(cls, &count);
    if (protocols == 0)
        return count == 0;

    for (unsigned int n = 0; n < count; n++) {
        const char* name = protocol_getName(protocols[n]);
        if (name == 0)
            continue;
        std::string normalized = swiftProtocolMangledName(name);
        if (!contains(result, normalized))
            result.push_back(normalized);
    }

    free((void*)protocols);
    return true;
}

bool ObjCMetadata::readProtocolFromRuntime(const std::string& name, Protocol& result) const {
    // The runtime keys protocols by their mangled name, which is what the image
    // holds as well.
    ::Protocol* protocol = objc_getProtocol(name.c_str());
    if (protocol == nil)
        return false;

    result.name = name;

    unsigned int count = 0;
    ::Protocol* __unsafe_unretained* parents = protocol_copyProtocolList(protocol, &count);
    if (parents != 0) {
        for (unsigned int n = 0; n < count; n++) {
            const char* parentName = protocol_getName(parents[n]);
            if (parentName != 0)
                result.protocols.push_back(swiftProtocolMangledName(parentName));
        }
        free(parents);
    }

    // The instance properties and the class properties of a protocol are two
    // lists of their own, and protocol_copyPropertyList() only hands out the
    // first of them.
    objc_property_t* propertyLists[2];
    std::vector<Property>* propertyTargets[2] = { &result.properties, &result.classProperties };
    unsigned int propertyCounts[2] = { 0, 0 };
    propertyLists[0] = protocol_copyPropertyList(protocol, &propertyCounts[0]);
    propertyLists[1] = protocol_copyPropertyList2(protocol, &propertyCounts[1], YES, NO);

    for (unsigned int list = 0; list < 2; list++) {
        if (propertyLists[list] == 0)
            continue;
        for (unsigned int n = 0; n < propertyCounts[list]; n++) {
            Property property;
            const char* propertyName = property_getName(propertyLists[list][n]);
            const char* attributes = property_getAttributes(propertyLists[list][n]);
            if (propertyName != 0)
                property.name = propertyName;
            if (attributes != 0)
                property.attributes = attributes;
            if (!property.name.empty())
                propertyTargets[list]->push_back(property);
        }
        free(propertyLists[list]);
    }

    struct {
        std::vector<Method>* target;
        BOOL required;
        BOOL instance;
    } groups[] = {
        { &result.requiredInstanceMethods, YES, YES },
        { &result.optionalInstanceMethods, NO, YES },
        { &result.requiredClassMethods, YES, NO },
        { &result.optionalClassMethods, NO, NO },
    };

    for (unsigned int g = 0; g < sizeof(groups) / sizeof(*groups); g++) {
        struct objc_method_description* descriptions =
            protocol_copyMethodDescriptionList(protocol, groups[g].required, groups[g].instance, &count);
        if (descriptions == 0)
            continue;
        for (unsigned int n = 0; n < count; n++) {
            const char* selector = descriptions[n].name != 0 ? sel_getName(descriptions[n].name) : 0;
            if (selector == 0 || isCompilerGeneratedMethod(selector))
                continue;
            Method method;
            method.name = selector;
            if (descriptions[n].types != 0)
                method.types = descriptions[n].types;
            groups[g].target->push_back(method);
        }
        free(descriptions);
    }

    return true;
}

void ObjCMetadata::readProtocols(uint64_t listAddress, std::vector<std::string>& result) const {
    if (listAddress == 0)
        return;

    uint64_t count = fixups.resolve(listAddress);
    if (count == 0 || count > kMaxListEntries)
        return;

    for (uint64_t n = 0; n < count; n++) {
        uint64_t slot = listAddress + 8 + n * 8;
        uint64_t protocolAddress = fixups.resolve(slot);
        if (protocolAddress == 0) {
            // A protocol defined in another image; only a mapped image can say
            // which one it is.
            std::string external = stripClassSymbolPrefix(symbolNameAtAddress(fixups.getExternalAddress(slot)));
            if (!external.empty() && !contains(result, external))
                result.push_back(external);
            continue;
        }
        // protocol_t: isa, mangledName, ...
        std::string name = readString(fixups.resolve(protocolAddress + 8));
        if (!name.empty() && !contains(result, name))
            result.push_back(name);
    }
}

bool ObjCMetadata::readClass(uint64_t address, Class& result) const {
    // class_t: isa, superclass, cache (two words), bits
    uint8_t classData[40];
    if (!architecture.readAtAddress(address, classData, sizeof(classData)))
        return false;

    // The low bits of `bits` are flags, not part of the address.
    uint64_t roAddress = classRoAddress(address);
    if (roAddress == 0)
        return false;

    // class_ro_t: flags, instanceStart, instanceSize, reserved, ivarLayout,
    //             name, baseMethods, baseProtocols, ivars, weakIvarLayout,
    //             baseProperties
    uint8_t ro[72];
    if (!architecture.readAtAddress(roAddress, ro, sizeof(ro)))
        return false;

    uint32_t flags = readUint32(ro);
    result.isRoot = (flags & kClassFlagRoot) != 0;
    result.name = readString(fixups.resolve(roAddress + 24));
    if (result.name.empty())
        return false;

    if (fixups.isImport(address + 8)) {
        // The superclass lives in another image; the fixup names it.
        result.superclass = stripClassSymbolPrefix(fixups.getImportName(address + 8));
        result.superclassKnown = !result.superclass.empty();
    } else if (fixups.isExternal(address + 8)) {
        // Same situation, but for an image dyld mapped into this process: the
        // pointer is an address of the other image, so ask the dynamic linker.
        result.superclass = stripClassSymbolPrefix(symbolNameAtAddress(fixups.getExternalAddress(address + 8)));
        result.superclassKnown = !result.superclass.empty();
    } else {
        uint64_t superclassAddress = fixups.resolve(address + 8);
        if (superclassAddress != 0) {
            result.superclass = readClassName(superclassAddress);
            result.superclassKnown = !result.superclass.empty();
        } else {
            result.superclassKnown = result.isRoot;
        }
    }

    // Ivars and properties read fine out of the image, mapped or not.
    readIvars(fixups.resolve(roAddress + 48), result.ivars);
    readProperties(fixups.resolve(roAddress + 64), result.properties);

    // The method and protocol lists of a shared cache image cannot be read out
    // of the image: dyld stores them in __TEXT.__objc_methlist as eight byte
    // entries in an encoding of its own, with the selector strings moved out of
    // the image entirely. The runtime has already decoded all of that, so ask
    // it -- but only after checking that it really is this very class, or the
    // answer would describe some other image's class of the same name.
    uint64_t processClassAddress = 0;
    if (architecture.getFile()->getMappedBase() != 0) {
        uint64_t candidate = address + (uint64_t)architecture.getFile()->getMappedSlide();
        ::Class runtimeClass = objc_getClass(result.name.c_str());
        if (runtimeClass != nil && (uint64_t)(uintptr_t)runtimeClass == candidate)
            processClassAddress = candidate;
    }

    if (processClassAddress != 0) {
        if (!readProtocolsFromRuntime(processClassAddress, result.protocols))
            readProtocols(fixups.resolve(roAddress + 40), result.protocols);

        std::vector<Method> methods;
        if (readMethodsFromRuntime(processClassAddress, false, methods) && !methods.empty())
            result.instanceMethods.swap(methods);
        else
            readMethods(fixups.resolve(roAddress + 32), result.instanceMethods);

        methods.clear();
        if (readMethodsFromRuntime(processClassAddress, true, methods) && !methods.empty())
            result.classMethods.swap(methods);
        else
            readMetaclassMethods(address, result.classMethods);

        return true;
    }

    readProtocols(fixups.resolve(roAddress + 40), result.protocols);
    readMethods(fixups.resolve(roAddress + 32), result.instanceMethods);
    readMetaclassMethods(address, result.classMethods);

    return true;
}

void ObjCMetadata::readMetaclassMethods(uint64_t classAddress, std::vector<Method>& result) const {
    // Class methods are the instance methods of the metaclass.
    uint64_t metaclassAddress = fixups.resolve(classAddress);
    if (metaclassAddress == 0)
        return;

    uint64_t metaclassRo = classRoAddress(metaclassAddress);
    uint8_t metaclassData[72];
    if (metaclassRo != 0 && architecture.readAtAddress(metaclassRo, metaclassData, sizeof(metaclassData)) &&
        (readUint32(metaclassData) & kClassFlagMeta) != 0) {
        readMethods(fixups.resolve(metaclassRo + 32), result);
    }
}

bool ObjCMetadata::readCategory(uint64_t address, Category& result) const {
    // category_t: name, cls, instanceMethods, classMethods, protocols,
    //             instanceProperties, classProperties
    uint8_t category[56];
    if (!architecture.readAtAddress(address, category, sizeof(category)))
        return false;

    // The name of a category_t is the one in the parentheses; the class it
    // extends is the classref next to it.
    result.name = readString(fixups.resolve(address));
    if (result.name.empty())
        return false;

    result.className = readClassReferenceName(address + 8);

    readProtocols(fixups.resolve(address + 32), result.protocols);
    readProperties(fixups.resolve(address + 40), result.properties);

    uint64_t instanceMethods = fixups.resolve(address + 16);
    uint64_t classMethods = fixups.resolve(address + 24);
    result.instanceMethodCount = readListCount(instanceMethods);
    result.classMethodCount = readListCount(classMethods);

    // An image dyld mapped into this process keeps its method lists in an
    // encoding of its own whose offsets point into a string pool that belongs
    // to no single image, so they cannot be decoded from here (see the progress
    // notes for the measurements). The methods are not lost by that: the
    // runtime merges every category into its class, and the listing of that
    // class already contains them.
    if (mappedBase() == 0) {
        readMethods(instanceMethods, result.instanceMethods);
        readMethods(classMethods, result.classMethods);
    }

    return true;
}

bool ObjCMetadata::readProtocol(uint64_t address, Protocol& result) const {
    // protocol_t: isa, mangledName, protocols, instanceMethods, classMethods,
    //             optionalInstanceMethods, optionalClassMethods,
    //             instanceProperties, size, flags, _extendedMethodTypes,
    //             _demangledName, _classProperties
    uint8_t protocol[96];
    if (!architecture.readAtAddress(address, protocol, sizeof(protocol)))
        return false;

    result.name = readString(fixups.resolve(address + 8));
    if (result.name.empty())
        return false;

    // The same applies to the method lists of a protocol, and the runtime has
    // an API that hands out a protocol by name -- so ask it.
    if (mappedBase() != 0) {
        Protocol fromRuntime;
        if (readProtocolFromRuntime(result.name, fromRuntime)) {
            result = fromRuntime;
            return true;
        }
    }

    readProtocols(fixups.resolve(address + 16), result.protocols);
    readProperties(fixups.resolve(address + 56), result.properties);
    readMethods(fixups.resolve(address + 24), result.requiredInstanceMethods);
    readMethods(fixups.resolve(address + 32), result.requiredClassMethods);
    readMethods(fixups.resolve(address + 40), result.optionalInstanceMethods);
    readMethods(fixups.resolve(address + 48), result.optionalClassMethods);
    if (readUint32(protocol + 64) >= kProtocolSizeWithClassProperties)
        readProperties(fixups.resolve(address + kProtocolClassPropertiesOffset), result.classProperties);

    return true;
}

#pragma mark - Rendering

namespace {

/**
 * "Ti,N,V_baseProperty" -> type "i", attributes "(nonatomic)". The declared
 * property name comes from the property itself, the ivar name from V.
 *
 * The codes are the ones the runtime documents: R is a read-only property and
 * & is a strong one, C a copy, W a weak one and N a non-atomic one. The
 * spelling that is written out is the current one, so & becomes strong.
 */
void parsePropertyAttributes(const std::string& attributes, bool classProperty,
                            std::string& type, std::string& rendered) {
    std::vector<std::string> flags;
    std::string getter;
    std::string setter;
    bool nonatomic = false;
    bool readonly = false;
    bool isWeak = false;
    std::string memory;

    size_t position = 0;
    while (position <= attributes.size()) {
        size_t end = attributes.find(',', position);
        if (end == std::string::npos)
            end = attributes.size();
        std::string token = attributes.substr(position, end - position);
        if (!token.empty()) {
            char code = token[0];
            if (code == 'T') {
                type = token.substr(1);
            } else if (code == 'V') {
                // The backing ivar; not shown, the property name is enough.
            } else if (code == 'C') {
                memory = "copy";
            } else if (code == '&') {
                memory = "strong";
            } else if (code == 'R') {
                readonly = true;
            } else if (code == 'W') {
                isWeak = true;
            } else if (code == 'N') {
                nonatomic = true;
            } else if (code == 'G') {
                getter = token.substr(1);
            } else if (code == 'S') {
                setter = token.substr(1);
            }
        }
        if (end >= attributes.size())
            break;
        position = end + 1;
    }

    if (classProperty)
        flags.push_back("class");
    if (nonatomic)
        flags.push_back("nonatomic");
    if (readonly)
        flags.push_back("readonly");
    if (isWeak)
        flags.push_back("weak");
    else if (!memory.empty())
        flags.push_back(memory);
    if (!getter.empty())
        flags.push_back("getter=" + getter);
    if (!setter.empty())
        flags.push_back("setter=" + setter);

    rendered = "(";
    for (unsigned int n = 0; n < flags.size(); n++) {
        if (n > 0)
            rendered += ", ";
        rendered += flags[n];
    }
    rendered += ")";
}

} // namespace

void ObjCMetadata::appendProperties(std::string& result, const std::vector<Property>& properties,
                                    bool classProperty) const {
    for (unsigned int n = 0; n < properties.size(); n++) {
        std::string typeEncoding;
        std::string attributes;
        parsePropertyAttributes(properties[n].attributes, classProperty, typeEncoding, attributes);

        std::string declaration;
        size_t position = 0;
        ObjCType type = ObjCTypeDecoder::decodeType(typeEncoding, position);
        if (type.isEmpty())
            declaration = "id " + properties[n].name;
        else
            declaration = type.declaration(properties[n].name);

        result += "@property ";
        result += attributes;
        result += " ";
        result += declaration;
        result += ";\n";
    }
}

void ObjCMetadata::appendMethods(std::string& result, const std::vector<Method>& methods,
                                 bool classMethod) const {
    for (unsigned int n = 0; n < methods.size(); n++) {
        std::string declaration = ObjCTypeDecoder::decodeMethod(methods[n].name, methods[n].types, classMethod);
        if (declaration.empty())
            continue;
        result += declaration;
        result += ";\n";
    }
}

std::string ObjCMetadata::render(const Class& info, bool metaclass) const {
    std::string result;

    if (metaclass) {
        result += "// Class methods of ";
        result += info.name;
        result += " (_OBJC_METACLASS_$_";
        result += info.name;
        result += ")\n";
    }

    result += "@interface ";
    result += info.name;
    if (!metaclass) {
        if (info.superclassKnown && !info.superclass.empty())
            result += " : " + info.superclass;
        else if (!info.isRoot)
            result += " /* superclass is imported and could not be named */";
    }
    if (!metaclass && !info.protocols.empty()) {
        result += " <";
        appendJoined(result, info.protocols, ", ");
        result += ">";
    }
    result += "\n";

    if (!metaclass && !info.ivars.empty()) {
        result += "{\n";
        for (unsigned int n = 0; n < info.ivars.size(); n++) {
            result += "    ";
            result += ObjCTypeDecoder::decodeIvar(info.ivars[n].type, info.ivars[n].name);
            result += ";\n";
        }
        result += "}\n";
    }

    if (!metaclass)
        appendProperties(result, info.properties, false);

    if (metaclass) {
        appendMethods(result, info.classMethods, true);
    } else {
        // A class declaration lists both, class methods first.
        if (!info.classMethods.empty()) {
            result += "\n/* class methods */\n";
            appendMethods(result, info.classMethods, true);
        }
        if (!info.instanceMethods.empty()) {
            result += "\n/* instance methods */\n";
            appendMethods(result, info.instanceMethods, false);
        }
    }

    result += "@end\n";
    return result;
}

std::string ObjCMetadata::renderCategory(const Category& info) const {
    std::string result = "@interface ";
    if (info.className.empty())
        result += "/* unknown class */";
    else
        result += info.className;
    result += " (";
    result += info.name;
    result += ")";
    if (!info.protocols.empty()) {
        result += " <";
        appendJoined(result, info.protocols, ", ");
        result += ">";
    }
    result += "\n";

    appendProperties(result, info.properties, false);

    if (!info.classMethods.empty()) {
        result += "\n/* class methods */\n";
        appendMethods(result, info.classMethods, true);
    } else if (info.classMethodCount > 0) {
        result += "\n/* ";
        result += describeCount(info.classMethodCount, "class method");
        result += " that the runtime has merged into ";
        result += info.className.empty() ? "the class" : info.className;
        result += " */\n";
    }

    if (!info.instanceMethods.empty()) {
        result += "\n/* instance methods */\n";
        appendMethods(result, info.instanceMethods, false);
    } else if (info.instanceMethodCount > 0) {
        result += "\n/* ";
        result += describeCount(info.instanceMethodCount, "instance method");
        result += " that the runtime has merged into ";
        result += info.className.empty() ? "the class" : info.className;
        result += " */\n";
    }

    result += "@end\n";
    return result;
}

std::string ObjCMetadata::renderProtocol(const Protocol& info) const {
    std::string result = "@protocol ";
    result += info.name;
    if (!info.protocols.empty()) {
        result += " <";
        appendJoined(result, info.protocols, ", ");
        result += ">";
    }
    result += "\n";

    if (!info.requiredInstanceMethods.empty() || !info.requiredClassMethods.empty() ||
        !info.properties.empty() || !info.classProperties.empty()) {
        result += "\n@required\n";
        appendProperties(result, info.properties, false);
        if (!info.classProperties.empty()) {
            // The properties of the protocol itself, rather than of whatever
            // conforms to it, are a group of their own.
            result += "\n";
            appendProperties(result, info.classProperties, true);
        }
        appendMethods(result, info.requiredClassMethods, true);
        appendMethods(result, info.requiredInstanceMethods, false);
    }

    if (!info.optionalInstanceMethods.empty() || !info.optionalClassMethods.empty()) {
        result += "\n@optional\n";
        appendMethods(result, info.optionalClassMethods, true);
        appendMethods(result, info.optionalInstanceMethods, false);
    }

    result += "\n@end\n";
    return result;
}

bool ObjCMetadata::hasClasses() const {
    parse();
    return !classes.empty();
}

std::vector<std::string> ObjCMetadata::getClassNames() const {
    parse();
    std::vector<std::string> names;
    names.reserve(classes.size());
    for (unsigned int n = 0; n < classes.size(); n++)
        names.push_back(classes[n]->name);
    return names;
}

bool ObjCMetadata::hasClass(const std::string& name) const {
    parse();
    for (unsigned int n = 0; n < classes.size(); n++) {
        if (classes[n]->name == name)
            return true;
    }
    return false;
}

std::vector<std::string> ObjCMetadata::getCategoryNames() const {
    parse();
    std::vector<std::string> names;
    names.reserve(categories.size());
    for (unsigned int n = 0; n < categories.size(); n++) {
        std::string name = categories[n]->className;
        name += "(";
        name += categories[n]->name;
        name += ")";
        names.push_back(name);
    }
    return names;
}

std::string ObjCMetadata::getCategoryDefinition(const std::string& className,
                                                const std::string& category) const {
    parse();
    for (unsigned int n = 0; n < categories.size(); n++) {
        if (categories[n]->name == category && categories[n]->className == className)
            return renderCategory(*categories[n]);
    }
    return std::string();
}

std::vector<std::string> ObjCMetadata::getProtocolNames() const {
    parse();
    std::vector<std::string> names;
    names.reserve(protocols.size());
    for (unsigned int n = 0; n < protocols.size(); n++)
        names.push_back(protocols[n]->name);
    return names;
}

bool ObjCMetadata::hasProtocol(const std::string& name) const {
    parse();
    for (unsigned int n = 0; n < protocols.size(); n++) {
        if (protocols[n]->name == name)
            return true;
    }
    return false;
}

std::string ObjCMetadata::getProtocolDefinition(const std::string& name) const {
    parse();
    for (unsigned int n = 0; n < protocols.size(); n++) {
        if (protocols[n]->name == name)
            return renderProtocol(*protocols[n]);
    }
    return std::string();
}

std::string ObjCMetadata::getDefinition(const std::string& name, bool metaclass) const {
    parse();
    for (unsigned int n = 0; n < classes.size(); n++) {
        if (classes[n]->name == name)
            return render(*classes[n], metaclass);
    }

    // A "_OBJC_PROTOCOL_$_*" symbol names a protocol rather than a class, and
    // the two share the name space of the symbol table.
    if (!metaclass) {
        std::string protocol = getProtocolDefinition(name);
        if (!protocol.empty())
            return protocol;
    }
    return std::string();
}

std::string ObjCMetadata::getDefinitions() const {
    parse();
    std::string result;
    for (unsigned int n = 0; n < classes.size(); n++) {
        if (n > 0)
            result += "\n";
        result += render(*classes[n], false);
    }
    return result;
}

std::string ObjCMetadata::getAllDefinitions() const {
    parse();
    std::string result = getDefinitions();

    for (unsigned int n = 0; n < categories.size(); n++) {
        if (!result.empty())
            result += "\n";
        result += renderCategory(*categories[n]);
    }

    for (unsigned int n = 0; n < protocols.size(); n++) {
        if (!result.empty())
            result += "\n";
        result += renderProtocol(*protocols[n]);
    }

    return result;
}
