#ifndef OBJCTYPEDECODER_H
#define OBJCTYPEDECODER_H

#include "macho_global.h"

#include <stddef.h>
#include <string>

/**
 * Turns Objective-C type encodings into something readable.
 *
 * A type is decoded into a prefix and a suffix so that a declaration can be
 * built as `prefix + name + suffix`. That is what makes `char _name[32]`
 * possible, which a plain string concatenation could not express:
 *
 *   "i"       -> prefix "int ",   suffix ""
 *   "^i"      -> prefix "int *",  suffix ""
 *   "[32c]"   -> prefix "char ",  suffix "[32]"
 *   "@\"NSString\"" -> prefix "NSString *", suffix ""
 */
class EXPORT ObjCType
{
public:
    ObjCType() {}
    ObjCType(const std::string& aPrefix, const std::string& aSuffix) : prefix(aPrefix), suffix(aSuffix) {}

    /** Builds the declaration, e.g. "char _name[32]". */
    std::string declaration(const std::string& name) const;

    bool isEmpty() const { return prefix.empty() && suffix.empty(); }

    std::string prefix;
    std::string suffix;
};

class EXPORT ObjCTypeDecoder
{
public:
    /**
     * Decodes the type starting at `position`, which is advanced past it.
     * An empty result means the encoding could not be understood.
     */
    static ObjCType decodeType(const std::string& encoding, size_t& position);

    /**
     * Decodes the field list of a struct or union that has no name of its own
     * ("{?={_NSRange=QQ}QQ}"). Such a type is rendered inline, which keeps the
     * declaration valid C.
     */
    static std::string decodeAnonymousBody(const std::string& encoding, size_t& position, char close);

    /** Decodes an ivar type encoding ("[32c]") into "char _name[32]". */
    static std::string decodeIvar(const std::string& encoding, const std::string& name);

    /**
     * Decodes a method type encoding such as "v32@0:8q16@24" into a readable
     * declaration such as "- (void)doThing:(long long)arg1 with:(id)arg2".
     */
    static std::string decodeMethod(const std::string& selector, const std::string& encoding, bool isClassMethod);
};

#endif // OBJCTYPEDECODER_H
