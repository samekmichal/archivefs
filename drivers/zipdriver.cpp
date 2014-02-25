/* Project:  ArchiveFS
 * Author:   Michal SAMEK
 * Email:    xsamek01@fit.vutbr.cz
 * Desc:     Driver for manipulating zip archives
 * Modified: 04/2012
 */

#include <iostream>
#include <cstring>
#include <cstdlib>
#include <climits>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <pthread.h>

#include "zipdriver.hpp"
#include "filesystem.hpp"
#include "membuffer.hpp"
#include "filebuffer.hpp"

class ZipDriverFactory: public AbstractFactory {
public:
  ArchiveDriver* getDriver(const char* path, bool create) {
    return new ZipDriver(path, create);
  }
};

extern "C" {
  DriverHandle* REGISTER_DRIVER () {
    DriverHandle* h = new DriverHandle;
    h->archive_types.push_back(new ArchiveType("zip", "application/zip",
                                               new ZipDriverFactory, true));
    return h;
  }
}

ZipDriver::ZipDriver(const char* _archive, bool create_archive)
  : ArchiveDriver(_archive) {
  int err;
  if (create_archive) {
    zip_file = zip_open(_archive, ZIP_CREATE, &err);
    if (zip_file == NULL) {
      cerr << "ZipDriver: " << strerror(err) << endl;
      throw ArchiveError();
    }
  } else {
    zip_file = zip_open(_archive, 0, NULL);
    if (zip_file == NULL) {
      cerr << "ZipDriver: " << zip_strerror(zip_file) << endl;
      throw ArchiveError();
    }
  }
  return;
}


ZipDriver::~ZipDriver() {
  if (zip_close(zip_file) == -1)
    cerr << "ZipDriver: " << zip_strerror(zip_file) << endl;
  return;
}

bool ZipDriver::open(FileNode* node) {
  ZipFileData* casted_data = static_cast<ZipFileData*>(node->data);

  casted_data->zip_file_data = zip_fopen_index(zip_file, casted_data->index, 0);

  int bytes_to_read = node->getSize();
  pthread_rwlock_wrlock(&(node->lock));
  try {
    node->buffer = new Buffer(bytes_to_read);
  }
  catch (...) {
    pthread_rwlock_unlock(&(node->lock));
    node->buffer = NULL;
    return false;
  }

  int read_bytes;
  int read_offset = 0;
  char tmp_buf[Buffer::BLOCK_SIZE];
  while (bytes_to_read > 0) {
    read_bytes = zip_fread(casted_data->zip_file_data, tmp_buf, Buffer::BLOCK_SIZE);
    if (read_bytes < 0) return false;
    node->buffer->write(tmp_buf, read_bytes, read_offset);
    bytes_to_read -= read_bytes;
    read_offset += read_bytes;
  }
  pthread_rwlock_unlock(&(node->lock));
  zip_fclose(casted_data->zip_file_data);
  return true;
}

int ZipDriver::read(FileNode* node, char* buffer, size_t bytes, offset_t offset) {
  pthread_rwlock_rdlock(&(node->lock));
  int read_bytes = node->buffer->read(buffer, bytes, offset);
  pthread_rwlock_unlock(&(node->lock));
  return read_bytes;
}

void ZipDriver::close(FileNode* node) {
  pthread_rwlock_wrlock(&(node->lock));
  if (node->buffer->release())
    node->buffer = NULL;
  pthread_rwlock_unlock(&(node->lock));
}

bool ZipDriver::buildFileSystem(FileSystem* fs) {
  if (fs == NULL) return false;

  int num_files = zip_get_num_files(zip_file);

  FileNode *node = NULL;
  char *zip_pathname = NULL;
  char *pathname, *ptr;
  struct zip_stat zip_info;
  zip_stat_init(&zip_info);
  int retcode = 0;
  bool new_node;
  enum FileNode::NodeType node_type;


  for (int i = 0; i < num_files; ++i) {
    zip_pathname = const_cast<char*>(zip_get_name(zip_file, i, 0));

    pathname = strdup(zip_pathname);
    ptr = pathname;

    /* Posun na poslední PLATNÝ znak */
    while (*ptr) ++ptr;
    --ptr;

    /* Pokud je poslední platný znak slash, jedná se o adresář */
    if (*ptr == '/'){
      node_type = FileNode::DIR_NODE;
      *ptr = '\0';
    } else
      node_type = FileNode::FILE_NODE;

    node = fs->find(pathname);

    if (node != NULL) {
      new_node = false;
      if (node->data == NULL) {
        node->data = new ZipFileData(i);
      } else
        static_cast<ZipFileData*>(node->data)->index = i;
    } else {
      node = new FileNode(pathname, new ZipFileData(i), node_type);
      new_node = true;
    }
    free(pathname);

    /* nasleduje zjisteni a zpracovani informaci/atributu souboru */
    retcode = zip_stat_index(zip_file, i, 0, &zip_info);
    if (retcode != 0) {
      cerr << "ZipDriver: " << zip_strerror(zip_file) << endl;
    }

    node->setSize(signed(zip_info.size));
    node->file_info.st_atime =
      node->file_info.st_ctime =
      node->file_info.st_mtime = zip_info.mtime;


    /* pokud jsme vytvareli novy uzel, pripojime jej do 'stromu' */
    if (new_node) {
      try {
        fs->append(node);
      }
      catch (FileSystem::AlreadyExists& existing) {
        existing.node->file_info = node->file_info;
        if (existing.node->data == NULL) existing.node->data = new ZipFileData;
        memcpy(existing.node->data, node->data, sizeof(ZipFileData));
        delete node;
      }
    }
  }

  return true;
}

ssize_t ZipDriver::zipUserFunctionCallback(void *state, void *data, size_t len, enum zip_source_cmd cmd) {
    ZipCallBack *callbck = reinterpret_cast<ZipCallBack*>(state);
    switch (cmd) {
        case ZIP_SOURCE_OPEN: {
            callbck->pos = 0;
            return 0;
        }

        case ZIP_SOURCE_READ: {
            int read_bytes = callbck->buffer->read((char*)data, len, callbck->pos);
            callbck->pos += read_bytes;
            return read_bytes;
        }

        case ZIP_SOURCE_STAT: {
            struct zip_stat* info = reinterpret_cast<struct zip_stat*>(data);
            zip_stat_init(info);
            info->size = callbck->buffer->length();
            info->mtime = callbck->mtime;
            return sizeof(struct zip_stat);
        }

        case ZIP_SOURCE_FREE: {
            delete callbck;
            return 0;
        }

        default: {
            return 0;
        }

    }
}

bool ZipDriver::saveArchive(FileMap* files, FileList* deleted) {
  FileNode* node;
  /* Názvy adresářů musí končit slashem */
  char dir_pathname[PATH_MAX];
  ZipFileData* data = NULL;
  struct zip_source* zip_src;
  struct zip* zip_file_new = NULL;

  string output_name = archive_path;
  if (keep_original) {
    generateNewArchiveName(output_name);
    zip_file_new = zip_open(output_name.c_str(), ZIP_CREATE|ZIP_EXCL, NULL);
    if (zip_file_new == NULL) return false;
  }

  if (!keep_original) {
    for (FileList::iterator it = deleted->begin(); it != deleted->end(); ++it) {
      node = *it;

      /* Soubor pravděpodobně není přítomen v archivů - nebyl mu přiřazen index */
      if (node->data == NULL) continue;
      data = static_cast<ZipFileData*>(node->data);
      zip_delete(zip_file, data->index);
    }
  }

  for (FileMap::iterator it = files->begin(); it != files->end(); ++it) {
    node = (*it).second;
    data = static_cast<ZipFileData*>(node->data);

    /* Vytvářím zcela nový archív ********************************************/
    if (keep_original) {
      if (node->type == FileNode::DIR_NODE) {
        strcpy(dir_pathname, node->pathname);
        strcat(dir_pathname, "/");
        zip_add_dir(zip_file_new, dir_pathname);
      } else if (node->data != NULL) {
        zip_src = zip_source_zip(zip_file_new, zip_file, data->index, 0, 0, -1);
        zip_add(zip_file_new, node->pathname, zip_src);
      } else {
        zip_src = zip_source_function(zip_file, zipUserFunctionCallback,
                (void*)new ZipCallBack(node->buffer, node->file_info.st_mtime));
        zip_add(zip_file, node->pathname, zip_src);
      }
      continue;
    }

    /* Modifikace archivu ****************************************************/

    /* Pokud se soubor nezměnil a ani nebyl přejmenován ...*/
    if (!node->changed && !node->original_pathname) continue;

    if (node->data == NULL) {
      /* Uzel v archivu neexistuje */
      if (node->type == FileNode::DIR_NODE) {
        strcpy(dir_pathname, node->pathname);
        strcat(dir_pathname, "/");
        zip_add_dir(zip_file, dir_pathname);
      } else {
        zip_src = zip_source_function(zip_file, zipUserFunctionCallback,
                  (void*)new ZipCallBack(node->buffer, node->file_info.st_mtime));
        zip_add(zip_file, node->pathname, zip_src);
      }
    } else {
      /* Uzel v archivu existuje */
      if (node->original_pathname) {
        /* Uzel byl přejmenován */
        if (node->type == FileNode::DIR_NODE) {
          strcpy(dir_pathname, node->pathname);
          strcat(dir_pathname, "/");
          zip_rename(zip_file, data->index, dir_pathname);
        } else
          zip_rename(zip_file, data->index, node->pathname);
      }
      if (node->changed) {
        /* Data uzlu byla změněna */
        zip_src = zip_source_function(zip_file, zipUserFunctionCallback,
                  (void*)new ZipCallBack(node->buffer, node->file_info.st_mtime));
        zip_replace(zip_file, data->index, zip_src);
      }
    }
  }

  /* Pokud jsme vytvářeli nový soubor, musíme jej uvolnit a do zip_file hodit
   * původní handle
   */
  if (keep_original) {
    zip_close(zip_file_new);
  }

  return true;
}

// void ZipDriver::addDir(struct zip* archive, const char* path, int prefix_len) {
//   struct zip_source* src;
//   DIR* dir;
//   struct dirent* file;
//   struct stat fi;
//   int err;
//   char pathname[PATH_MAX];
//   char* name_ptr;
//   size_t path_len = strlen(path);
//   strcpy(pathname, path);
//   pathname[path_len] = '/';
//   name_ptr = pathname + path_len + 1;
//
//   dir = opendir(path);
//   while((file = readdir(dir)) != NULL) {
//     if (strcmp(file->d_name, ".") == 0 || strcmp(file->d_name, "..") == 0) continue;
//
//     strcpy(name_ptr, file->d_name);
//     if (lstat(pathname, &fi) == -1) {
//       err = errno;
//       cout << "ZipDriver: failed to add " << pathname << ": " << strerror(err) << endl;
//       continue;
//     }
//
//     if (S_ISDIR(fi.st_mode)) {
//       addDir(archive, pathname, prefix_len);
//     } else {
//       src = zip_source_file(archive, pathname, 0, 0);
//       zip_add(archive, path+prefix_len, src);
//     }
//   }
//   closedir(dir);
// }
//
// bool ZipDriver::createArchive(const char* source, const char* dest) {
//   struct zip* archive = zip_open(dest, ZIP_CREATE|ZIP_EXCL, NULL);
//   if (archive == NULL) return false;
//
//   addDir(archive, source, strlen(source)+1);
//
//   zip_close(archive);
//   return true;
// }