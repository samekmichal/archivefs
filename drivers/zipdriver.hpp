/* Project:  ArchiveFS
 * Author:   Michal SAMEK
 * Email:    xsamek01@fit.vutbr.cz
 * Desc:     Header file for zipdriver.cpp
 * Modified: 04/2012
 */

#ifndef ZIP_DRIVER_HPP
#define ZIP_DRIVER_HPP

#include <cstdio>
#include <sys/stat.h>
#include <zip.h>

#include "archivedriver.hpp"

using namespace std;

class ZipFileData: public FileData {
public:
  ZipFileData(int _index = -1) {
    index = _index;
    zip_file_data = NULL;
  }
  struct zip_file* zip_file_data;
  int index;
  ~ZipFileData() {
    if (zip_file_data) {
      zip_file_data = NULL;
    }
  }
};

class ZipDriver: public ArchiveDriver {
public:
  ZipDriver(const char* _archive, bool create_archive);
  ~ZipDriver();

  bool open(FileNode* node);
  int read(FileNode* node, char* buffer, size_t bytes, offset_t offset);
  void close(FileNode* node);

  bool buildFileSystem(FileSystem* fs);
//   static bool createArchive(const char* source, const char* dest);

private:
  struct zip* zip_file;
  bool saveArchive(FileMap* files, FileList* deleted);
//   static void addDir(struct zip* archive, const char* path, int prefix_len);
  static ssize_t zipUserFunctionCallback(void*, void*, size_t, enum zip_source_cmd);

  struct ZipCallBack {
    ZipCallBack(Buffer* buf, time_t t) : buffer(buf), mtime(t) {}
    offset_t pos;
    Buffer* buffer;
    time_t mtime;
  };
};

#endif