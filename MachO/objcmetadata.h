#ifndef OBJCMETADATA_H
#define OBJCMETADATA_H

#include "macho_global.h"
#include "machofixups.h"

#include <stdint.h>
#include <string>
#include <vector>

class MachOArchitecture;

/**
 * Reads the Objective-C metadata of a Mach-O image -- __objc_classlist and
 * everything it points at -- and renders it as source declarations.
 *
 * Everything comes out of the image itself: no external tool is run, so it
 * works for any file on disk, including one built for another platform. An
 * image that only exists inside the dyld shared cache is read out of the
 * mapping dyld made of it, which is also the only way to get at a system
 * library that has no file of its own any more.
 */
class EXPORT ObjCMetadata
{
public:
    explicit ObjCMetadata(const MachOArchitecture& architecture);
    ~ObjCMetadata();

    /** True when the image has an Objective-C class list at all. */
    bool hasClasses() const;

    /** The names of all classes, in the order of the class list. */
    std::vector<std::string> getClassNames() const;

    /** True when the image defines a class with that name. */
    bool hasClass(const std::string& name) const;

    /**
     * The "@interface ... @end" declaration of one class, or an empty string
     * when the image has no such class. With `metaclass` the class methods are
     * listed instead of the instance methods.
     */
    std::string getDefinition(const std::string& name, bool metaclass) const;

    /** The declarations of every class, separated by a blank line. */
    std::string getDefinitions() const;

    /** The names of the categories of the image, as "Class(Category)". */
    std::vector<std::string> getCategoryNames() const;

    /** The "@interface Class (Category)" declaration of one category. */
    std::string getCategoryDefinition(const std::string& className,
                                      const std::string& category) const;

    /** The names of the protocols the image declares itself. */
    std::vector<std::string> getProtocolNames() const;

    /** True when the image declares a protocol with that name. */
    bool hasProtocol(const std::string& name) const;

    /** The "@protocol ... @end" declaration of one protocol. */
    std::string getProtocolDefinition(const std::string& name) const;

    /**
     * Every declaration of the image: classes, then categories, then the
     * protocols it declares itself, separated by a blank line.
     */
    std::string getAllDefinitions() const;

private:
    struct Class;
    struct Category;
    struct Protocol;
    struct Method;
    struct Ivar;
    struct Property;

    // Not copyable: the parsed classes are owned.
    ObjCMetadata(const ObjCMetadata&);
    ObjCMetadata& operator=(const ObjCMetadata&);

    void parse() const;
    bool readClass(uint64_t address, Class& result) const;
    bool readCategory(uint64_t address, Category& result) const;
    bool readProtocol(uint64_t address, Protocol& result) const;
    void readMethods(uint64_t listAddress, std::vector<Method>& result) const;

    /** Number of entries a method / property list header claims, or 0. */
    uint32_t readListCount(uint64_t listAddress) const;

    /** The class methods, i.e. the instance methods of the metaclass. */
    void readMetaclassMethods(uint64_t classAddress, std::vector<Method>& result) const;
    void readIvars(uint64_t listAddress, std::vector<Ivar>& result) const;
    void readProperties(uint64_t listAddress, std::vector<Property>& result) const;
    void readProtocols(uint64_t listAddress, std::vector<std::string>& result) const;
    std::string readString(uint64_t address) const;
    std::string readClassName(uint64_t classAddress) const;

    /**
     * Name of the class a classref points at, whatever way the pointer is
     * encoded: a bind, a pointer into a mapped image, or a plain address.
     */
    std::string readClassReferenceName(uint64_t slotAddress) const;

    std::string render(const Class& info, bool metaclass) const;
    std::string renderCategory(const Category& info) const;
    std::string renderProtocol(const Protocol& info) const;

    /**
     * "@property (nonatomic) NSString *name;" lines, one per property. With
     * `classProperty` the class keyword is added, as in
     * "@property (class, readonly) NSString *name;".
     */
    void appendProperties(std::string& result, const std::vector<Property>& properties,
                          bool classProperty) const;

    /** "- (void)foo;" / "+ (id)bar;" lines, one per method. */
    void appendMethods(std::string& result, const std::vector<Method>& methods,
                       bool classMethod) const;

    /**
     * Address of the class_ro_t of the class at `classAddress`, in the unslid
     * address space of the image.
     */
    uint64_t classRoAddress(uint64_t classAddress) const;

    /**
     * Unslid address of the class_ro_t behind a class_data_bits_t::bits value
     * of an image that dyld mapped into this process.
     */
    uint64_t decodeMappedClassData(uint64_t bits) const;

    /**
     * Method lists of an image that dyld mapped into this process, taken from
     * the runtime rather than from the image. Returns false when the runtime
     * does not know the class.
     */
    bool readMethodsFromRuntime(uint64_t processClassAddress, bool metaclass,
                                std::vector<Method>& result) const;

    /** Protocol names of such a class, taken from the runtime. */
    bool readProtocolsFromRuntime(uint64_t processClassAddress,
                                  std::vector<std::string>& result) const;

    /**
     * Method list of one protocol, taken from the runtime. Returns false when
     * the runtime does not know the protocol.
     */
    bool readProtocolFromRuntime(const std::string& name, Protocol& result) const;

    /** True when a protocol of that name was already read. */
    bool hasProtocolNamed(const std::string& name) const;

    /** The image as dyld mapped it, or 0 when it was read from disk. */
    const uint8_t* mappedBase() const;

    const MachOArchitecture& architecture;
    MachOFixups fixups;
    mutable bool parsed;
    mutable std::vector<Class*> classes;
    mutable std::vector<Category*> categories;
    mutable std::vector<Protocol*> protocols;
};

#endif // OBJCMETADATA_H
