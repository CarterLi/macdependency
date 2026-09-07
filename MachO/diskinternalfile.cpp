#include "diskinternalfile.h"
#include "machoexception.h"

#include <sys/stat.h>

DiskInternalFile::DiskInternalFile(const std::string& filename) :
InternalFile(filename), fileSize(0), lastWriteTime(0)
{
  file.open(filename, std::ios_base::in | std::ios_base::binary);
  if (file.fail()) {
    throw MachOException("Couldn't open file '" + filename + "'.");
  }

  struct stat buffer;
  if (stat(filename.c_str(), &buffer) >= 0) {
    fileSize = buffer.st_size;
    lastWriteTime = buffer.st_mtime;
  }
}

DiskInternalFile::~DiskInternalFile()
{
  if (file.is_open())
    file.close();
}

unsigned long long DiskInternalFile::getSize() const {
  return fileSize;
}

bool DiskInternalFile::seek(long long int position) {
  file.seekg(position, std::ios_base::beg);
  if (file.fail()) {
    file.clear();
    return false;
  }
  return true;
}

std::streamsize DiskInternalFile::read(char* buffer, std::streamsize size) {
  file.read(buffer, size);
  if (file.fail()) {
    file.clear();
    return file.gcount();
  }
  // TODO: handle badbit
  return size;
}

long long int DiskInternalFile::getPosition() {
  return file.tellg();
}

time_t DiskInternalFile::getLastModificationTime() const {
  return lastWriteTime;
}
