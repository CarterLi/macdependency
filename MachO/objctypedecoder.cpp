#include "objctypedecoder.h"

#include <stdio.h>
#include <vector>

namespace {

bool isDigit(char character) {
    return character >= '0' && character <= '9';
}

// r = const, n = in, N = out, o = inout, O = bycopy, R = byref, V = oneway.
// None of those is a type code itself.
bool isQualifier(char character) {
    return character == 'r' || character == 'n' || character == 'N' || character == 'o' ||
           character == 'O' || character == 'R' || character == 'V';
}

/** The frame size / offset digits that may follow a type. */
void skipDigits(const std::string& encoding, size_t& position) {
    while (position < encoding.size() && isDigit(encoding[position]))
        position++;
}

/**
 * Skips the body of a struct or union whose opening brace was already
 * consumed, leaving `position` behind the matching closing brace.
 */
void skipBody(const std::string& encoding, size_t& position, char open, char close) {
    int depth = 0;
    while (position < encoding.size()) {
        char character = encoding[position];
        if (character == open) {
            depth++;
        } else if (character == close) {
            if (depth == 0) {
                position++;
                return;
            }
            depth--;
        }
        position++;
    }
}

/** Everything up to the field list ('=') or the closing brace. */
std::string readName(const std::string& encoding, size_t& position, char close) {
    std::string name;
    while (position < encoding.size() && encoding[position] != '=' && encoding[position] != close) {
        name.push_back(encoding[position]);
        position++;
    }
    return name;
}

ObjCType applyQualifiers(const ObjCType& type, const std::string& qualifiers) {
    if (qualifiers.empty())
        return type;
    return ObjCType(qualifiers + type.prefix, type.suffix);
}

std::string argumentName(size_t index) {
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "arg%lu", (unsigned long)index);
    return std::string(buffer);
}

} // namespace

std::string ObjCType::declaration(const std::string& name) const {
    std::string result = prefix + name + suffix;
    // Without a name the type would otherwise end in the space of the prefix.
    while (name.empty() && !result.empty() && result[result.size() - 1] == ' ')
        result.erase(result.size() - 1);
    return result;
}

ObjCType ObjCTypeDecoder::decodeType(const std::string& encoding, size_t& position) {
    std::string qualifiers;
    while (position < encoding.size() && isQualifier(encoding[position])) {
        if (encoding[position] == 'r')
            qualifiers = "const ";
        position++;
    }
    if (position >= encoding.size())
        return ObjCType();

    char code = encoding[position];

    switch (code) {
        case 'c': position++; return applyQualifiers(ObjCType("char ", ""), qualifiers);
        case 'C': position++; return applyQualifiers(ObjCType("unsigned char ", ""), qualifiers);
        case 's': position++; return applyQualifiers(ObjCType("short ", ""), qualifiers);
        case 'S': position++; return applyQualifiers(ObjCType("unsigned short ", ""), qualifiers);
        case 'i': position++; return applyQualifiers(ObjCType("int ", ""), qualifiers);
        case 'I': position++; return applyQualifiers(ObjCType("unsigned int ", ""), qualifiers);
        case 'l': position++; return applyQualifiers(ObjCType("long ", ""), qualifiers);
        case 'L': position++; return applyQualifiers(ObjCType("unsigned long ", ""), qualifiers);
        case 'q': position++; return applyQualifiers(ObjCType("long long ", ""), qualifiers);
        case 'Q': position++; return applyQualifiers(ObjCType("unsigned long long ", ""), qualifiers);
        case 'f': position++; return applyQualifiers(ObjCType("float ", ""), qualifiers);
        case 'd': position++; return applyQualifiers(ObjCType("double ", ""), qualifiers);
        case 'D': position++; return applyQualifiers(ObjCType("long double ", ""), qualifiers);
        case 'B': position++; return applyQualifiers(ObjCType("bool ", ""), qualifiers);
        case 'v': position++; return applyQualifiers(ObjCType("void ", ""), qualifiers);
        case '*': position++; return applyQualifiers(ObjCType("char *", ""), qualifiers);
        case ':': position++; return applyQualifiers(ObjCType("SEL ", ""), qualifiers);
        case '#': position++; return applyQualifiers(ObjCType("Class ", ""), qualifiers);
        case '?': position++; return applyQualifiers(ObjCType("id ", ""), qualifiers);

        case '@': {
            position++;
            if (position < encoding.size() && encoding[position] == '?') {
                position++;
                // The signature of a block is not part of the encoding.
                return applyQualifiers(ObjCType("id /* block */ ", ""), qualifiers);
            }
            if (position < encoding.size() && encoding[position] == '"') {
                position++;
                std::string name;
                while (position < encoding.size() && encoding[position] != '"')
                    name.push_back(encoding[position++]);
                if (position < encoding.size())
                    position++;
                if (name.empty() || name == "?")
                    return applyQualifiers(ObjCType("id ", ""), qualifiers);
                return applyQualifiers(ObjCType(name + " *", ""), qualifiers);
            }
            return applyQualifiers(ObjCType("id ", ""), qualifiers);
        }

        case '^': {
            position++;
            if (position < encoding.size() && encoding[position] == '?') {
                position++;
                return applyQualifiers(ObjCType("void *", ""), qualifiers);
            }
            ObjCType pointee = decodeType(encoding, position);
            if (pointee.isEmpty())
                return applyQualifiers(ObjCType("void *", ""), qualifiers);
            return applyQualifiers(ObjCType(pointee.prefix + "*", pointee.suffix), qualifiers);
        }

        case 'b': {
            position++;
            skipDigits(encoding, position);
            return applyQualifiers(ObjCType("unsigned int ", ""), qualifiers);
        }

        case 'j': {
            // _Complex: the encoding names the underlying type afterwards.
            position++;
            ObjCType component = decodeType(encoding, position);
            return applyQualifiers(component, qualifiers);
        }

        case '{':
        case '(': {
            char close = (code == '{') ? '}' : ')';
            std::string keyword = (code == '{') ? "struct" : "union";
            position++;
            std::string name = readName(encoding, position, close);
            if (!name.empty() && name != "?") {
                skipBody(encoding, position, code, close);
                return applyQualifiers(ObjCType(name + " ", ""), qualifiers);
            }
            // A struct or union without a name of its own (the encoding uses
            // '?') is written out inline so that the declaration stays valid.
            std::string body = decodeAnonymousBody(encoding, position, close);
            if (body.empty())
                return applyQualifiers(ObjCType(keyword + " ", ""), qualifiers);
            return applyQualifiers(ObjCType(keyword + " { " + body + " } ", ""), qualifiers);
        }

        case '[': {
            position++;
            std::string count;
            while (position < encoding.size() && isDigit(encoding[position]))
                count.push_back(encoding[position++]);
            ObjCType element = decodeType(encoding, position);
            if (position < encoding.size() && encoding[position] == ']')
                position++;
            if (element.isEmpty())
                return applyQualifiers(ObjCType("", "[" + count + "]"), qualifiers);
            return applyQualifiers(ObjCType(element.prefix, "[" + count + "]" + element.suffix), qualifiers);
        }

        default:
            position++;
            return applyQualifiers(ObjCType("id ", ""), qualifiers);
    }
}

std::string ObjCTypeDecoder::decodeAnonymousBody(const std::string& encoding, size_t& position, char close) {
    std::string body;
    if (position < encoding.size() && encoding[position] == '=')
        position++;

    while (position < encoding.size() && encoding[position] != close) {
        std::string fieldName;
        if (encoding[position] == '"') {
            position++;
            while (position < encoding.size() && encoding[position] != '"')
                fieldName.push_back(encoding[position++]);
            if (position < encoding.size())
                position++;
        }
        ObjCType field = decodeType(encoding, position);
        if (field.isEmpty())
            break;
        if (!body.empty())
            body += " ";
        body += field.declaration(fieldName);
        body += ";";
    }
    if (position < encoding.size() && encoding[position] == close)
        position++;
    return body;
}

std::string ObjCTypeDecoder::decodeIvar(const std::string& encoding, const std::string& name) {
    size_t position = 0;
    ObjCType type = decodeType(encoding, position);
    if (type.isEmpty()) {
        // A few ivars carry no type encoding at all; "id" is the safe guess.
        if (encoding.empty())
            return "id " + name;
        return encoding + " " + name;
    }
    return type.declaration(name);
}

std::string ObjCTypeDecoder::decodeMethod(const std::string& selector, const std::string& encoding, bool isClassMethod) {
    if (encoding.empty())
        return std::string();

    size_t position = 0;
    ObjCType returnType = decodeType(encoding, position);
    // An unreadable return type is no reason to hide the method.
    if (returnType.isEmpty())
        returnType = ObjCType("id ", "");

    skipDigits(encoding, position);
    // The first two arguments are always self and _cmd.
    decodeType(encoding, position);
    skipDigits(encoding, position);
    decodeType(encoding, position);
    skipDigits(encoding, position);
    std::vector<std::string> parts;
    std::string current;
    for (size_t n = 0; n < selector.size(); n++) {
        if (selector[n] == ':') {
            parts.push_back(current);
            current.clear();
        } else {
            current.push_back(selector[n]);
        }
    }

    std::string result = isClassMethod ? "+ (" : "- (";
    result += returnType.declaration("");
    result += ")";

    if (parts.empty()) {
        result += selector;
        return result;
    }

    for (size_t n = 0; n < parts.size(); n++) {
        ObjCType argument = decodeType(encoding, position);
        skipDigits(encoding, position);
        if (argument.isEmpty())
            argument = ObjCType("id ", "");
        if (n > 0)
            result += " ";
        result += parts[n] + ":(" + argument.declaration(argumentName(n + 1)) + ")";
    }
    return result;
}
