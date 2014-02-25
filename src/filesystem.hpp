/* Project:  ArchiveFS
 * Author:   Michal SAMEK
 * Email:    xsamek01@fit.vutbr.cz
 * Desc:     Header for filesystem.cpp
 * Modified: 04/2012
 */

#ifndef FILESYSTEM_HPP
#define FILESYSTEM_HPP


#include <map>
#include <vector>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/statvfs.h>
#include <dlfcn.h>
#include <pthread.h>

#include "archivedriver.hpp"
#include "drivers.hpp"
#include "filenode.hpp"

using namespace std;

/// Třída reprezentující souborový systém uvnitř archivu.
/** Popis třídy
 *  Operace nad filesystémem vrací kladné errno.
 */
class FileSystem {
public:
  /// Konstruktor
  FileSystem(const char* _archive_name,
             bool create_archive,
             ArchiveType* driver_hndl);

  /// Destruktor
  ~FileSystem();

  /// Připojí uzel odkazovaný FileNode* do file_map.
  /** Aktualizuje taky pole potomků nadřazených uzlů a vytváří vazbu
   *  mezi uzlem new_node a adresářem, jenž jej obsahuje.
   *  @param new_node je ukazatel na připojovaný uzel
   *  @throw AlreadyExists pokud již new_node ve file_map existuje
   */
  void append(FileNode* new_node);
  void appendToNode(FileNode* new_node, FileNode* parent_node);
  bool take(FileNode* node);

  /// Vyhledává v poli file_map uzel s cestou filename
  /** @param filename název hledaného uzlu
   *  @return FileNode* ukazatel na nalezený uzel nebo NULL
   */
  FileNode* find(const char* filename);
  FileNode* getRoot() const;

  ///
  vector<FileNode*> getFileNames() const;
  struct statvfs archive_statvfs;
  bool write_support;

  int open(FileNode* node, int flags);
  int mknod(const char* path, mode_t mode);
  int create(const char* path, mode_t mode, FileNode** new_node);
  int mkdir(const char* path, mode_t mode);
  int rename(FileNode* node, const char* new_path);
  void repath(FileNode* node, const char* path);
  int read(FileNode* node, char* buffer, size_t bytes, off_t offset);
  int write(FileNode* node, const char* buffer, size_t length, off_t offset);
  int truncate(FileNode* node, ssize_t size);
  int remove(FileNode* node);
  int access(FileNode* node, int mask, uid_t uid, gid_t gid);
  int parentAccess(const char* path, int mask, uid_t uid, gid_t gid);
  int utimens(FileNode* node, const struct timespec times[2]);
  void fillInBuffer(FileNode* node, ssize_t size = 0);
  void close(FileNode* node);
  struct stat* getAttr(FileNode* node);
  FileList* readDir(FileNode*);

  const char* archive_name;

  static char* path_to_drivers;
  static bool keep_trash;

  inline static void setBufferLimit(int limit) {
    Buffer::MEM_LIMIT = limit * 1024 * 1024;
  }

private:
  FileMap file_map;

  /// Seznam se smazanými FileNody
  /** Ukládají se pouze objekty vztažené k souborům nacházejícím se v archivu.
   */
  FileList removed_nodes;
  FileNode* root_node;
  bool changed;

  int archive_file; //file deskriptor
  void initStatvfs();
  bool releaseUnchanged();
  void removeTrash();
  bool isPathSearchable(FileNode* node, uid_t uid, gid_t gid);

  ArchiveDriver* driver;
  pthread_mutex_t fmap_mux;

public:
  /** \class FileNotFound
   *  Třída pro vyjímky, které jsou vyvolány pokud konkrétní soubor v archivu
   *  není nalezen
   */
  class FileNotFound {};

  /** \class AlreadyExists
   *  Vyjímka použitá v případě pokusu o přidání do file_map dříve přidaný uzel
   */
  class AlreadyExists {
  public:
    AlreadyExists(FileNode* _node) {node = _node;}
    FileNode* node;
  };
};

void printStat(struct stat*);

#endif
