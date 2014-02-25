/* Project:  ArchiveFS
 * Author:   Michal SAMEK
 * Email:    xsamek01@fit.vutbr.cz
 * Desc:     Driver for manipulating tar [GZIP] archives
 * Modified: 04/2012
 */

#include <iostream>
#include <cstring>
#include <cstdlib>
#include <fcntl.h>
#include <unistd.h>
#include <zlib.h>
#include <pthread.h>

#include "tardriver.hpp"
#include "filesystem.hpp"
#include "membuffer.hpp"
#include "filebuffer.hpp"

class TarDriverFactory: public AbstractFactory {
public:
  ArchiveDriver* getDriver(const char* path, bool create) {
    return new TarDriver(path, create);
  }
};

class TarGzDriverFactory: public AbstractFactory {
public:
  ArchiveDriver* getDriver(const char* path, bool create) {
    return new TarDriver(path, create, TarDriver::GZIP);
  }
};

extern "C" {
  DriverHandle* REGISTER_DRIVER () {
    DriverHandle* h = new DriverHandle;
    h->archive_types.push_back(new ArchiveType("tar", "application/x-tar",
                                               new TarDriverFactory));
    h->archive_types.push_back(new ArchiveType("tgz", "application/x-gzip",
                                               new TarGzDriverFactory));
    h->archive_types.push_back(new ArchiveType("tar.gz", "application/x-gzip",
                                               new TarGzDriverFactory));
    return h;
  }
}

TarDriver::TarDriver(const char* _archive, bool create_archive, enum Compression _comp)
  : ArchiveDriver(_archive) {

  if (create_archive) throw ArchiveError();

  compression_used = _comp;
  switch (_comp) {
    case GZIP:
      functions.openfunc = (openfunc_t)gzopen_frontend;
      functions.closefunc = (closefunc_t)gzclose;
      functions.readfunc = (readfunc_t)gzread;
      functions.writefunc = (writefunc_t)gzwrite;
      functions.seekfunc = (seekfunc_t)gzseek;
      break;

    default:
      functions.openfunc = ::open;
      functions.closefunc = ::close;
      functions.readfunc = ::read;
      functions.writefunc = ::write;
      functions.seekfunc = ::lseek;
      break;
  }

  if (tar_open(&tar_file, const_cast<char*>(_archive),
      (tartype_t*)&functions, O_RDONLY, 0644, TAR_VERBOSE) != 0) {
    throw ArchiveError();
  }


  return;
}

TarDriver::~TarDriver() {
  tar_close(tar_file);
}

bool TarDriver::open(FileNode* node) {
  if (compression_used != NONE) {
    TarFileData* casted_data = static_cast<TarFileData*>(node->data);

    offset_t bytes_to_read = node->getSize();
    pthread_rwlock_wrlock(&(node->lock));
    try {
        node->buffer = new Buffer(bytes_to_read);
    }
    catch (...) {
      pthread_rwlock_unlock(&(node->lock));
      node->buffer = NULL;
      return false;
    }

    int read_bytes = 0;

    unsigned bytes;
    unsigned read_offset = 0;
    functions.seekfunc(tar_file->fd, casted_data->offset, SEEK_SET);

    while (bytes_to_read > 0) {
      if (Buffer::BLOCK_SIZE > bytes_to_read) bytes = bytes_to_read;
      else bytes = Buffer::BLOCK_SIZE;

      char buf[Buffer::BLOCK_SIZE];
      read_bytes = functions.readfunc(tar_file->fd, buf, bytes);
      node->buffer->write(buf, read_bytes, read_offset);
      if (read_bytes < 0) return false;
      bytes_to_read -= read_bytes;
      read_offset += read_bytes;
    }
    pthread_rwlock_unlock(&(node->lock));
  }
  return true;
}

int TarDriver::read(FileNode* node, char* buffer, size_t bytes, offset_t offset) {
  TarFileData* casted_data = static_cast<TarFileData*>(node->data);
  return (pread(tar_fd(tar_file), buffer, bytes, casted_data->offset+offset));
}

void TarDriver::close(FileNode* node) {
  if (compression_used != NONE) {
    pthread_rwlock_wrlock(&(node->lock));
    if (node->buffer->release())
      node->buffer = NULL;
    pthread_rwlock_unlock(&(node->lock));
  }
}

bool TarDriver::buildFileSystem(FileSystem* fs) {
  if (fs == NULL) return false;

  FileNode *node = NULL;
  bool new_node;
  char *tar_pathname = NULL;
  char *pathname, *ptr;
  enum FileNode::NodeType node_type;
  off_t offset;

  /* Pro jistotu nastavime ukazatel na data souboru na zacatek */
  functions.seekfunc(tar_fd(tar_file), 0, SEEK_SET);

  while(th_read(tar_file) == 0) {
    tar_pathname = th_get_pathname(tar_file);
    pathname = strdup(tar_pathname);
    ptr = pathname;

    while (*ptr) ++ptr;
    --ptr;

    if (*ptr == '/') {
      node_type = FileNode::DIR_NODE;
      *ptr = '\0';
    } else
      node_type = FileNode::FILE_NODE;

    offset = functions.seekfunc(tar_fd(tar_file), 0, SEEK_CUR);
    tar_skip_regfile(tar_file);

    node = fs->find(pathname);

    if (node != NULL) {
      new_node = false;
      if (node->data == NULL)
        node->data = new TarFileData(offset);
      else
        static_cast<TarFileData*>(node->data)->offset = offset;
    } else {
      node = new FileNode(pathname, new TarFileData(offset), node_type);
      new_node = true;
    }

    free(pathname);

    /* nasleduje zjisteni a zpracovani informaci/atributu souboru */
    node->setSize(th_get_size(tar_file));
    node->file_info.st_atime =
      node->file_info.st_mtime =
      node->file_info.st_ctime = th_get_mtime(tar_file);

    if (respect_rights) {
      node->file_info.st_mode = th_get_mode(tar_file);
      node->file_info.st_uid = th_get_uid(tar_file);
      node->file_info.st_gid = th_get_gid(tar_file);
    }

    /* pokud jsme vytvareli novy uzel, pripojime jej do 'stromu' */
    if (new_node) {
      try {
        fs->append(node);
      }
      catch (FileSystem::AlreadyExists& existing) {
        existing.node->file_info = node->file_info;
        if (existing.node->data == NULL) existing.node->data = new TarFileData;
        memcpy(existing.node->data, node->data, sizeof(TarFileData));
        delete node;
      }
    }
  }

  return true;
}

bool TarDriver::saveArchive(FileMap* files, FileList* deleted) {
  (void)files;
  (void)deleted;

  return false;
}

// bool TarDriver::saveArchive(FileSystem* fs) {
//   FileSystem::FileMap::iterator it = fs->file_map.begin();
//   return true;
// }

int gzopen_frontend(char *pathname, int oflags, int mode) {
  char *gzoflags;
  gzFile gzf;
  int fd;

  switch (oflags & O_ACCMODE)
  {
  case O_WRONLY:
    gzoflags = (char*)"wb";
    break;
  case O_RDONLY:
    gzoflags = (char*)"rb";
    break;
  default:
  case O_RDWR:
    errno = EINVAL;
    return -1;
  }

  fd = open(pathname, oflags, mode);
  if (fd == -1)
    return -1;

  if ((oflags & O_CREAT) && fchmod(fd, mode))
    return -1;

  gzf = gzdopen(fd, gzoflags);
  if (!gzf)
  {
    errno = ENOMEM;
    return -1;
  }

  /* This is a bad thing to do on big-endian lp64 systems, where the
     size and placement of integers is different than pointers.
     However, to fix the problem 4 wrapper functions would be needed and
     an extra bit of data associating GZF with the wrapper functions.  */
  return (intptr_t)gzf;
}