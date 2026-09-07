/*
 *  dyldcacheimage.cpp
 *  MachO
 *
 *  Reads the symbol table of a library that has no file on disk, i.e. a library
 *  that only exists inside the dyld shared cache ("/usr/lib/libz.1.dylib" and
 *  friends since macOS 11).
 *
 *  Those libraries cannot be opened with ifstream but they *can* be dlopen()ed,
 *  and once they are mapped the load commands and the LINKEDIT data are
 *  reachable in memory. This file copies them into a standalone Mach-O image.
 */

#include "dyldcacheimage.h"
#include "machoexception.h"

#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <mach-o/nlist.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#ifndef LC_DYLD_EXPORTS_TRIE
#define LC_DYLD_EXPORTS_TRIE 0x80000033u
#endif

namespace {

const uint8_t kFlagReexport = 0x08;
const uint8_t kFlagStubAndResolver = 0x10;

const uint32_t kMaxLoadCommands = 100000u;
const uint32_t kMaxSymbols = 20000000u;
const unsigned long long kMaxHeaderSize = 64ull * 1024ull * 1024ull;

/** Reads a little endian base 128 encoded integer (dyld's uleb128). */
uint64_t readUleb(const uint8_t** cursor, const uint8_t* end)
{
    uint64_t result = 0;
    unsigned int shift = 0;
    while (*cursor < end) {
        uint8_t byte = *(*cursor)++;
        result |= (uint64_t)(byte & 0x7fu) << shift;
        if ((byte & 0x80u) == 0)
            break;
        shift += 7;
    }
    return result;
}

/** Collects every symbol of a dyld export trie. */
class ExportTrie
{
public:
    typedef std::pair<std::string, uint64_t> Entry;

    ExportTrie(const uint8_t* start, uint32_t size)
    : start_(start), end_(start != 0 ? start + size : 0) {}

    void walk()
    {
        if (start_ != 0 && end_ > start_)
            visit(0, std::string(), 0);
    }

    const std::vector<Entry>& entries() const { return entries_; }

private:
    void visit(uint32_t offset, const std::string& prefix, int depth)
    {
        if (depth > 64)
            return;

        const uint8_t* node = start_ + offset;
        if (node >= end_)
            return;

        uint64_t terminalSize = readUleb(&node, end_);
        if (terminalSize > (uint64_t)(end_ - node))
            return;
        const uint8_t* children = node + terminalSize;

        if (terminalSize > 0 && node < end_) {
            const uint8_t* cursor = node;
            uint64_t flags = readUleb(&cursor, end_);
            uint64_t value = 0;
            if ((flags & kFlagReexport) != 0) {
                // reexport: import ordinal followed by the imported name
                readUleb(&cursor, end_);
                while (cursor < end_ && *cursor != 0)
                    cursor++;
                cursor++;
            } else {
                value = readUleb(&cursor, end_);
            }
            if ((flags & kFlagStubAndResolver) != 0)
                readUleb(&cursor, end_);

            if (!prefix.empty())
                entries_.push_back(Entry(prefix, value));
        }

        if (children >= end_)
            return;
        uint8_t childCount = *children++;
        for (uint8_t i = 0; i < childCount; i++) {
            if (children >= end_)
                return;
            const uint8_t* terminator = children;
            while (terminator < end_ && *terminator != 0)
                terminator++;
            if (terminator >= end_)
                return;

            std::string next(prefix);
            next.append((const char*)children, (size_t)(terminator - children));

            const uint8_t* cursor = terminator + 1;
            uint64_t childOffset = readUleb(&cursor, end_);
            visit((uint32_t)childOffset, next, depth + 1);
            children = cursor;
        }
    }

    const uint8_t* start_;
    const uint8_t* end_;
    std::vector<Entry> entries_;
};

/** "a/b/libSystem.dylib" -> "libSystem.dylib" */
std::string baseNameOf(const std::string& path)
{
    size_t slash = path.rfind('/');
    return (slash == std::string::npos) ? path : path.substr(slash + 1);
}

/** "libSystem.B.dylib" -> "libSystem" (drops the ABI / version letter) */
std::string stemOf(const std::string& baseName)
{
    size_t dot = baseName.find('.');
    return (dot == std::string::npos) ? baseName : baseName.substr(0, dot);
}

/**
 * Locates the loaded image belonging to `path`. Returns -1 when not found.
 * dyld resolves aliases such as /usr/lib/libSystem.dylib to the real cache
 * image /usr/lib/libSystem.B.dylib, so the file name alone is not always
 * enough to find it.
 */
int findImageIndex(const std::string& path)
{
    uint32_t count = _dyld_image_count();

    for (uint32_t i = 0; i < count; i++) {
        const char* name = _dyld_get_image_name(i);
        if (name != 0 && path == name)
            return (int)i;
    }

    std::string baseName = baseNameOf(path);
    if (baseName.empty())
        return -1;
    std::string requestedStem = stemOf(baseName);

    int stemMatch = -1;
    for (uint32_t i = 0; i < count; i++) {
        const char* name = _dyld_get_image_name(i);
        if (name == 0)
            continue;
        std::string imageBase = baseNameOf(name);
        if (imageBase == baseName)
            return (int)i;
        if (stemMatch < 0 && stemOf(imageBase) == requestedStem)
            stemMatch = (int)i;
    }
    return stemMatch;
}

/** Appends `size` zero bytes until the buffer is aligned on `alignment`. */
void align(std::vector<char>& buffer, size_t alignment)
{
    while ((buffer.size() % alignment) != 0)
        buffer.push_back(0);
}

template <typename T>
void append(std::vector<char>& buffer, const T& value)
{
    const char* bytes = reinterpret_cast<const char*>(&value);
    buffer.insert(buffer.end(), bytes, bytes + sizeof(T));
}

} // namespace

DyldCacheImage::DyldCacheImage()
{
}

DyldCacheImage::~DyldCacheImage()
{
}

void DyldCacheImage::load(const std::string& path)
{
    // RTLD_NOLOAD first: if dyld already has the image there is no need to run
    // its initializers. Only when it is not loaded yet do we pay for that.
    //
    // Known residual risk: a handful of system libraries abort or crash inside
    // their initializers when they are dlopen()ed by an unrelated process
    // (ActionKit, ActionKitUI, WorkflowUI, libcrypto, libssl and the
    // TextToSpeechKonaSupport voices -- 24 out of the 3463 images of this
    // machine's cache). Accepted for now; if it ever matters, load() should be
    // moved into a short lived helper process.
    void* handle = dlopen(path.c_str(), RTLD_NOLOAD);
    if (handle == 0)
        handle = dlopen(path.c_str(), RTLD_LAZY);
    if (handle == 0)
        throw MachOException("Couldn't open file '" + path + "'.");

    // NOTE: the handle is deliberately never dlclose()d. Closing it would run
    // the image's terminators and could unmap memory we still point at, while
    // keeping it costs nothing for shared cache images (dyld keeps them mapped
    // for the lifetime of the process anyway).

    int index = findImageIndex(path);
    if (index < 0)
        throw MachOException("Couldn't find loaded image '" + path + "'.");

    const struct mach_header_64* header =
        (const struct mach_header_64*)_dyld_get_image_header((uint32_t)index);
    intptr_t slide = _dyld_get_image_vmaddr_slide((uint32_t)index);
    if (header == 0)
        throw MachOException("Invalid image header for '" + path + "'.");

    if (header->magic != MH_MAGIC_64)
        throw MachOException("Only 64 bit images can be read from the dyld shared cache ('" + path + "').");

    if (header->ncmds == 0 || header->ncmds > kMaxLoadCommands || header->sizeofcmds == 0)
        throw MachOException("Invalid load commands in '" + path + "'.");

    unsigned long long headerSize =
        (unsigned long long)sizeof(struct mach_header_64) + header->sizeofcmds;
    if (headerSize > kMaxHeaderSize)
        throw MachOException("Load commands of '" + path + "' are too large.");

    buffer.clear();
    const char* mappedHeader = reinterpret_cast<const char*>(header);
    buffer.insert(buffer.end(), mappedHeader, mappedHeader + headerSize);

    // ---- scan the load commands -------------------------------------------
    // Only offsets are kept: `buffer` is grown later on, which would invalidate
    // any pointer into it.
    size_t symtabCommandOffset = 0;
    uint32_t symbolOffset = 0;
    uint32_t numberOfSymbols = 0;
    uint32_t stringOffset = 0;
    uint32_t stringTableSize = 0;
    uint64_t linkeditVmAddress = 0;
    uint64_t linkeditVmSize = 0;
    uint64_t linkeditFileOffset = 0;
    uint32_t exportsTrieOffset = 0;
    uint32_t exportsTrieSize = 0;

    size_t offset = sizeof(struct mach_header_64);
    for (uint32_t i = 0; i < header->ncmds; i++) {
        if (offset + sizeof(struct load_command) > (size_t)headerSize)
            throw MachOException("Truncated load commands in '" + path + "'.");

        const struct load_command* command =
            reinterpret_cast<const struct load_command*>(&buffer[offset]);
        uint32_t cmd = command->cmd;
        uint32_t cmdSize = command->cmdsize;

        if (cmdSize < sizeof(struct load_command) || offset + cmdSize > (size_t)headerSize)
            throw MachOException("Invalid load command size in '" + path + "'.");

        if (cmd == LC_SEGMENT_64) {
            const struct segment_command_64* segment =
                reinterpret_cast<const struct segment_command_64*>(command);
            if (cmdSize >= sizeof(struct segment_command_64) &&
                strncmp(segment->segname, "__LINKEDIT", sizeof(segment->segname)) == 0) {
                linkeditVmAddress = segment->vmaddr;
                linkeditVmSize = segment->vmsize;
                linkeditFileOffset = segment->fileoff;
            }
        } else if (cmd == LC_SYMTAB) {
            const struct symtab_command* command2 =
                reinterpret_cast<const struct symtab_command*>(command);
            symtabCommandOffset = offset;
            symbolOffset = command2->symoff;
            numberOfSymbols = command2->nsyms;
            stringOffset = command2->stroff;
            stringTableSize = command2->strsize;
        } else if (cmd == LC_DYLD_EXPORTS_TRIE) {
            const struct linkedit_data_command* command2 =
                reinterpret_cast<const struct linkedit_data_command*>(command);
            exportsTrieOffset = command2->dataoff;
            exportsTrieSize = command2->datasize;
        }

        offset += cmdSize;
    }

    if (symtabCommandOffset == 0)
        throw MachOException("Image '" + path + "' has no symbol table.");
    if (linkeditVmSize == 0)
        throw MachOException("Image '" + path + "' has no __LINKEDIT segment.");

    // Offsets of a mapped image are file offsets relative to the beginning of
    // the file the image was loaded from -- for shared cache images that is the
    // cache itself, hence __LINKEDIT is shared by all images and only symoff /
    // stroff differ. The same formula works for ordinary mapped dylibs.
    const uint8_t* linkeditBase = reinterpret_cast<const uint8_t*>(linkeditVmAddress + slide);
    const uint8_t* linkeditEnd = linkeditBase + linkeditVmSize;

    // An empty symbol table is not an error: some cached images (several
    // libswift* dylibs for instance) keep LC_SYMTAB only as a placeholder and
    // carry their exports exclusively in the export trie.
    if (numberOfSymbols > kMaxSymbols)
        throw MachOException("Image '" + path + "' has a symbol table of implausible size.");
    if (symbolOffset < linkeditFileOffset || stringOffset < linkeditFileOffset)
        throw MachOException("Symbol table of '" + path + "' lies outside __LINKEDIT.");

    const uint8_t* symbolTable = linkeditBase + (symbolOffset - linkeditFileOffset);
    const uint8_t* stringTable = linkeditBase + (stringOffset - linkeditFileOffset);
    if (symbolTable + (uint64_t)numberOfSymbols * sizeof(struct nlist_64) > linkeditEnd ||
        stringTable >= linkeditEnd)
        throw MachOException("Symbol table of '" + path + "' lies outside __LINKEDIT.");

    const struct nlist_64* symbols = reinterpret_cast<const struct nlist_64*>(symbolTable);

    // ---- build a compact string table --------------------------------------
    // The shared cache uses one gigantic string table for all of its images
    // (hundreds of megabytes), so only the strings this image really references
    // are copied and every n_strx is rewritten.
    std::vector<char> strings;
    strings.push_back('\0'); // index 0 stays the empty string
    std::vector<uint32_t> newStringIndex(numberOfSymbols, 0);
    std::unordered_map<std::string, size_t> definedSymbols;

    for (uint32_t i = 0; i < numberOfSymbols; i++) {
        uint32_t stringIndex = symbols[i].n_un.n_strx;
        if (stringIndex == 0 || stringIndex >= stringTableSize) {
            newStringIndex[i] = 0;
            continue;
        }

        if (stringTable + stringIndex >= linkeditEnd) {
            newStringIndex[i] = 0;
            continue;
        }

        const char* name = reinterpret_cast<const char*>(stringTable) + stringIndex;
        size_t maxLength = (size_t)(stringTableSize - stringIndex);
        size_t remaining = (size_t)(linkeditEnd - (stringTable + stringIndex));
        if (remaining < maxLength)
            maxLength = remaining;

        size_t length = 0;
        while (length < maxLength && name[length] != '\0')
            length++;

        uint32_t offsetInNewTable = (uint32_t)strings.size();
        strings.insert(strings.end(), name, name + length + 1);
        newStringIndex[i] = offsetInNewTable;

        uint8_t type = symbols[i].n_type;
        if ((type & N_STAB) == 0 && (type & N_TYPE) != N_UNDF) {
            definedSymbols.insert(std::make_pair(std::string(name, length), (size_t)i));
        }
    }

    // ---- exports ------------------------------------------------------------
    // The export trie is authoritative. Symbols it exports but which the symbol
    // table does not mark as external are upgraded in place; symbols missing
    // from the symbol table altogether are appended.
    struct AdditionalSymbol {
        uint32_t stringIndex;
        uint64_t value;
    };
    std::vector<AdditionalSymbol> additionalSymbols;
    std::vector<uint8_t> upgradedTypes(numberOfSymbols, 0);
    bool hasUpgrades = false;

    if (exportsTrieSize > 0 && exportsTrieOffset >= linkeditFileOffset &&
        (uint64_t)exportsTrieOffset - linkeditFileOffset + exportsTrieSize <= linkeditVmSize) {
        const uint8_t* trie = linkeditBase + (exportsTrieOffset - linkeditFileOffset);
        if (trie + exportsTrieSize <= linkeditEnd) {
            ExportTrie walker(trie, exportsTrieSize);
            walker.walk();

            uint64_t imageBase = (uint64_t)header - (uint64_t)slide;

            const std::vector<ExportTrie::Entry>& exports = walker.entries();
            for (size_t i = 0; i < exports.size(); i++) {
                const std::string& name = exports[i].first;

                std::unordered_map<std::string, size_t>::const_iterator found =
                    definedSymbols.find(name);
                if (found != definedSymbols.end()) {
                    uint8_t& type = upgradedTypes[found->second];
                    type = symbols[found->second].n_type;
                    type |= N_EXT;
                    type &= ~N_PEXT;
                    hasUpgrades = true;
                    continue;
                }

                uint32_t stringOffset = (uint32_t)strings.size();
                strings.insert(strings.end(), name.begin(), name.end());
                strings.push_back('\0');

                AdditionalSymbol symbol;
                symbol.stringIndex = stringOffset;
                symbol.value = exports[i].second != 0 ? imageBase + exports[i].second : 0;
                additionalSymbols.push_back(symbol);
            }
        }
    }

    // ---- assemble ----------------------------------------------------------
    align(buffer, 8);
    uint32_t newSymbolOffset = (uint32_t)buffer.size();

    for (uint32_t i = 0; i < numberOfSymbols; i++) {
        struct nlist_64 entry = symbols[i];
        entry.n_un.n_strx = newStringIndex[i];
        if (hasUpgrades && upgradedTypes[i] != 0)
            entry.n_type = upgradedTypes[i];
        append(buffer, entry);
    }

    for (size_t i = 0; i < additionalSymbols.size(); i++) {
        struct nlist_64 entry;
        memset(&entry, 0, sizeof(entry));
        entry.n_un.n_strx = additionalSymbols[i].stringIndex;
        entry.n_type = N_SECT | N_EXT;
        entry.n_sect = 1;
        entry.n_desc = 0;
        entry.n_value = additionalSymbols[i].value;
        append(buffer, entry);
    }

    uint32_t newStringOffset = (uint32_t)buffer.size();
    buffer.insert(buffer.end(), strings.begin(), strings.end());

    // ---- patch the copied LC_SYMTAB ----------------------------------------
    struct symtab_command* patched =
        reinterpret_cast<struct symtab_command*>(&buffer[symtabCommandOffset]);
    patched->symoff = newSymbolOffset;
    patched->nsyms = numberOfSymbols + (uint32_t)additionalSymbols.size();
    patched->stroff = newStringOffset;
    patched->strsize = (uint32_t)strings.size();
}
