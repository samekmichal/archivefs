/* Project:  ArchiveFS
 * Author:   Michal SAMEK
 * Email:    xsamek01@fit.vutbr.cz
 * Desc:     Header file for isodriver.cpp
 * Modified: 04/2012
 */

#ifndef ISO_DRIVER_HPP
#define ISO_DRIVER_HPP

#include <sys/types.h>
#include <pthread.h>
#include "archivedriver.hpp"

#define LIBISO_SAFE_FOR_CPLUSPLUS yes
#define LIBISOFS_WITHOUT_LIBBURN yes

extern "C" {
  #include <libisofs/libisofs.h>
}
using namespace std;

class FileSystem;

class IsoFileData: public FileData {
public:
  IsoFileData(IsoFileSource* _data = NULL) {
    data = _data;
  }
  ~IsoFileData() {
    iso_file_source_unref(data);
  }

  IsoFileSource* data;
};


class IsoDriver: public ArchiveDriver {
public:
  IsoDriver(const char* _archive, bool create_archive);
  ~IsoDriver();

  bool open(FileNode* node);
  int read(FileNode* node, char* buffer, size_t bytes, offset_t offset);
  void close(FileNode* node);
  bool saveArchive(FileMap* files, FileList* deleted);

  bool buildFileSystem(FileSystem* fs);

private:
  IsoDataSource* iso_source;
  IsoImageFilesystem* iso_filesystem;
  bool buildDir(FileSystem* fs, IsoFileSource* dir);
  pthread_mutex_t mutex;

  /** \class BufferStream
   * "REdefinice" struktury IsoStream.
   */
  struct BufferStream {
    IsoStreamIface *iface;
    int refcount;
    Buffer* buffer;
    offset_t pos;
    ino_t ino_id;
    bool is_duplicate;

    BufferStream(Buffer* _buf) : iface(&buffer_stream_iface),
                                 refcount(1),
                                 buffer(_buf),
                                 pos(-1),
                                 ino_id(serial_id++),
                                 is_duplicate(false) {}
    static ino_t serial_id;
  };

  static IsoStreamIface buffer_stream_iface;
  static int bufferStreamOpen(IsoStream*);
  static int bufferStreamClose(IsoStream*);
  static off_t bufferStreamGetSize(IsoStream*);
  static int bufferStreamRead(IsoStream*, void*, size_t);
  static int bufferStreamIsRepeatable(IsoStream*);
  static void bufferStreamGetId(IsoStream*, unsigned int*, dev_t*, ino_t*);
  static void bufferStreamFree(IsoStream*);
  static int bufferStreamUpdateSize(IsoStream*);
  static IsoStream* bufferStreamGetInputStream(IsoStream*, int);
  static int bufferStreamCmpIno(IsoStream*, IsoStream*);
  static int bufferStreamCloneStream(IsoStream*, IsoStream**, int);
};

#endif
