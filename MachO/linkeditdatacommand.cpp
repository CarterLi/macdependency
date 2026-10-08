#include "linkeditdatacommand.h"
#include "machoheader.h"
#include "machofile.h"

#include <mach-o/loader.h>

LinkeditDataCommand::LinkeditDataCommand(MachOHeader* header) :
    LoadCommand(header), command(0), dataOffset(0), dataSize(0), bindOffset(0), bindSize(0),
    lazyBindOffset(0), lazyBindSize(0), commandSize(0)
{
    uint32_t cmd = file.readUint32();
    command = cmd;
    file.seek(offset);

    if (cmd == LC_DYLD_INFO || cmd == LC_DYLD_INFO_ONLY) {
        struct dyld_info_command command;
        file.readBytes((char*)&command, sizeof(command));
        commandSize = file.getUint32(command.cmdsize);
        // The generic payload of a dyld_info_command is its rebase stream.
        dataOffset = file.getUint32(command.rebase_off);
        dataSize = file.getUint32(command.rebase_size);
        bindOffset = file.getUint32(command.bind_off);
        bindSize = file.getUint32(command.bind_size);
        lazyBindOffset = file.getUint32(command.lazy_bind_off);
        lazyBindSize = file.getUint32(command.lazy_bind_size);
    } else {
        struct linkedit_data_command command;
        file.readBytes((char*)&command, sizeof(command));
        commandSize = file.getUint32(command.cmdsize);
        dataOffset = file.getUint32(command.dataoff);
        dataSize = file.getUint32(command.datasize);
    }
}
