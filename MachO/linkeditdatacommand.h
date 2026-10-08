#ifndef LINKEDITDATACOMMAND_H
#define LINKEDITDATACOMMAND_H

#include "loadcommand.h"

/**
 * Load commands that only point at a blob in __LINKEDIT, i.e. everything that
 * is laid out as a struct linkedit_data_command: LC_DYLD_CHAINED_FIXUPS,
 * LC_DYLD_EXPORTS_TRIE, LC_CODE_SIGNATURE and friends.
 *
 * LC_DYLD_INFO(_ONLY) shares the first two fields but carries the offsets of
 * the classic rebase / bind opcode streams behind them, so those are exposed
 * as well.
 */
class EXPORT LinkeditDataCommand : public LoadCommand
{
public:
    LinkeditDataCommand(MachOHeader* header);

    virtual unsigned int getSize() const { return commandSize; }

    /** The LC_* value this command was created for. */
    uint32_t getCommand() const { return command; }

    /** Offset of the payload relative to the beginning of the Mach-O image. */
    uint32_t getDataOffset() const { return dataOffset; }
    uint32_t getDataSize() const { return dataSize; }

    /** LC_DYLD_INFO(_ONLY): the classic bind opcode streams. */
    uint32_t getBindOffset() const { return bindOffset; }
    uint32_t getBindSize() const { return bindSize; }
    uint32_t getLazyBindOffset() const { return lazyBindOffset; }
    uint32_t getLazyBindSize() const { return lazyBindSize; }

private:
    uint32_t command;
    uint32_t dataOffset;
    uint32_t dataSize;
    uint32_t bindOffset;
    uint32_t bindSize;
    uint32_t lazyBindOffset;
    uint32_t lazyBindSize;
    unsigned int commandSize;
};

#endif // LINKEDITDATACOMMAND_H
