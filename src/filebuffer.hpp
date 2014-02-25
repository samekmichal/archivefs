/* Project:  ArchiveFS
 * Author:   Michal SAMEK
 * Email:    xsamek01@fit.vutbr.cz
 * Desc:     Wrapper class around system calls for manipulating files.
 * Modified: 4/2012
 */

#ifndef FILE_BUFFER_HPP
#define FILE_BUFFER_HPP

#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <unistd.h>

#include "bufferiface.hpp"

// IMPORTANT: if you change the template, dont forget to change the template length
#define FILENAME_TPL "/tmp/afs_buffer.XXXXXX"
#define FILENAME_LEN 23

using namespace std;

class FileBuffer: public BufferIface {
private:
  int fd;
  char* filename;
  offset_t _length;
  /// Definuje počet bytů po kolika bude duplikován FileBuffer
  static const unsigned READ_BLOCK_SIZE = 4*1024;

public:
  FileBuffer(offset_t size) {
    filename = new char[FILENAME_LEN];
    strcpy(filename, FILENAME_TPL);
    fd = ::mkstemp(filename);
    if (fd == -1) {
      cerr << "FileBufferStream error: " << strerror(errno) << endl;
      throw errno;
    }
    ::unlink(filename);
    _length = size;
  }

  FileBuffer(const FileBuffer& old) {
    fd = dup(old.fd);
//     lseek(fd, 0, SEEK_SET);
  }

  inline offset_t length() {
    return _length;
  }

  /**
   * Destruktor dealokuje paměť užívanou bufferem.
   */
  ~FileBuffer() {
    delete[] filename;
    ::close(fd);
  };

  size_t read(char* buffer, offset_t bytes, offset_t offset) const {
    return ::pread(fd, buffer, bytes, offset);
  }

  size_t write(const char* data, offset_t data_len, offset_t offset) {
    if (offset_t(offset+data_len) > _length) _length = offset + data_len;
    return ::pwrite(fd, data, data_len, offset);
  }

  void truncate(offset_t size) {
    ::ftruncate(fd, size);
  }

  inline int getFd() {
    return fd;
  }
};

#endif
