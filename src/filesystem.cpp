/* Project:  ArchiveFS
 * Author:   Michal SAMEK
 * Email:    xsamek01@fit.vutbr.cz
 * Desc:     File implementing FileSystem class - filesystem inside single archive
 * Modified: 04/2012
 */

#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <climits>
#include <iostream>
#include <algorithm>

#include <unistd.h>
#include <fcntl.h>

#include "filesystem.hpp"

char* FileSystem::path_to_drivers = NULL;
bool FileSystem::keep_trash = false;
offset_t Buffer::MEM_LIMIT;

/* FileSystem::konstruktor
 * - vytvoří odpovídající ovladač archivu
 * - vytvoří kořenový uzel
 * - nechá ovladačem vybudovat asociativní pole se soubory
 */
FileSystem::FileSystem(const char* _archive_name, bool create_archive, ArchiveType* archive_type)
  : changed(false),
    driver(NULL) {

  if (_archive_name == NULL || archive_type == NULL)
    throw ArchiveDriver::ArchiveError();

  archive_name = strdup(_archive_name);

  if (create_archive)
    archive_file = ::creat(_archive_name, /* rwx rw- rw- */
                           S_IRWXU | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH | S_IFREG);
  else
    archive_file = ::open(_archive_name, O_RDWR);

  if (archive_file == -1) {
    ::close(archive_file);
    cerr << "Error: Cannot open " << _archive_name << endl;
    free((void*)archive_name);
    throw ArchiveDriver::ArchiveError();
  }

  pthread_mutexattr_t mux_attr;
  pthread_mutexattr_init(&mux_attr);
  pthread_mutexattr_settype(&mux_attr, PTHREAD_MUTEX_RECURSIVE);
  pthread_mutex_init(&fmap_mux, &mux_attr);

  /* Vytvoření kořenového uzlu */
  root_node = new FileNode(NULL, NULL, FileNode::ROOT_NODE);

  write_support = archive_type->write_support;
  try {
    driver = archive_type->factory->getDriver(_archive_name, create_archive);
    if (driver == NULL) {
      pthread_mutex_destroy(&fmap_mux);
      throw ArchiveDriver::ArchiveError();
    }
    if (!create_archive)
      if (!driver->buildFileSystem(this)) {
        cerr << "Archive filesystem - is NOT built completely" << endl;
      }
  }
  catch (...) {
    free((void*)archive_name);
    ::close(archive_file);
    cerr << "Could not create filesystem for " << _archive_name << endl;
    pthread_mutex_destroy(&fmap_mux);
    delete root_node;
    delete driver;
    throw;
  }

  this->initStatvfs();
  ::close(archive_file); //initStatvfs potřebuje otevřený deskriptor

//   #ifndef NDEBUG
//   cout << _archive_name << " contains this nodes: " << endl;
//   for (FileMap::const_iterator it = file_map.begin();
//        it != file_map.end(); ++it) {
//     cout << (*it).first << endl;
//     cout << *((*it).second) << endl;
//
//   }
//   #endif

}

/* FileSystem::destruktor
 */
FileSystem::~FileSystem() {
  /* zapíšeme změny do archivu */
  if (changed) {
    if (!keep_trash) removeTrash();
    cout << "Changes in archive " << archive_name;
    if (driver->saveArchive(&file_map, &removed_nodes)) {
      cout << " have been successfuly written" << endl;
    } else {
      cout << " have NOT been successfuly written" << endl;
    }
  }
  free((void*)archive_name);

  /* Driver je třeba smazat před vymazáním obsahu filesystému.
   * libzip totiž potřebuje přistoupit k datům filesystému při uzavírání archivu
   */
  delete driver;

  for (FileMap::reverse_iterator it = file_map.rbegin(); it != file_map.rend(); ++it) {
    delete it->second;
  }

  for (FileList::iterator it = removed_nodes.begin(); it != removed_nodes.end(); ++it) {
    delete (*it);
  }

  delete root_node;
  pthread_mutex_destroy(&fmap_mux);

}


/* FileSystem::find
 * - hledá v asociativním poli file_map uzel s cestou pathname
 * - pokud nalezne, vrati ukazatel na nalezeny FileNode
 * - pokud nenalezna, vraci NULL
 */
FileNode* FileSystem::find(const char* pathname) {
  if (pathname == NULL) return root_node;

  pthread_mutex_lock(&fmap_mux);
  FileMap::const_iterator it = file_map.find(pathname);

  if (it == file_map.end()) {
    pthread_mutex_unlock(&fmap_mux);
    return NULL;
  } else {
    pthread_mutex_unlock(&fmap_mux);
    return (*it).second;
  }
}

FileNode* FileSystem::getRoot() const {
  return this->root_node;
}

/* FileSystem::append */
void FileSystem::append(FileNode* new_node) {
  pair<FileMap::iterator, bool> fmap_ret;

  pthread_mutex_lock(&fmap_mux);
    fmap_ret = file_map.insert(pair<const char*, FileNode*>(new_node->pathname, new_node));
  pthread_mutex_unlock(&fmap_mux);

  if (fmap_ret.second == false) {
    throw AlreadyExists(fmap_ret.first->second);
  }

  /* Aktualizace rodiřů/potomků - vyhledání adresáře obsahujícího
   * přidávaný soubor, pokud není nalezen vytvoříme ho.
   * Přidáme aktuální soubor do seznamu s potomky u rodiče a aktuálnímu
   * uzlu nastavíme ukazatel na nadřazený uzel. */


  FileNode* parent_node;
  char parent_name[PATH_MAX];
  /* Pokud jmeno zacina tam kde cesta, nachazi se v korenovem adresari */
  if (new_node->pathname == new_node->name_ptr)
    parent_node = root_node;
  else {
    /* Na chvili retezec rozsekneme */
    *(new_node->name_ptr-1) = '\0';
    strcpy(parent_name, new_node->pathname);
    *(new_node->name_ptr-1) = '/';
    parent_node = find(parent_name);
    if (parent_node == NULL) {
      parent_node = new FileNode(parent_name, NULL, FileNode::DIR_NODE);
      append(parent_node);
    }
  }

  new_node->parent = parent_node;
  parent_node->addChild(new_node);
}

bool FileSystem::take(FileNode* node) {
  pthread_mutex_lock(&fmap_mux);
    int ret = file_map.erase(node->pathname);
  pthread_mutex_unlock(&fmap_mux);

  if (ret == 0) return false;

  FileList* files = &(node->parent->children);
  FileList::iterator it = ::find(files->begin(), files->end(), node);

  if (it != files->end()) {
    files->erase(it);
    return true;
  }
  return false;
}

void FileSystem::appendToNode(FileNode* new_node, FileNode* parent_node) {
  pair<FileMap::iterator, bool> fmap_ret;

  pthread_mutex_lock(&fmap_mux);
    fmap_ret = file_map.insert(pair<const char*, FileNode*>(new_node->pathname, new_node));
  pthread_mutex_unlock(&fmap_mux);

  if (fmap_ret.second == false) {
    throw AlreadyExists(new_node);
  }

  parent_node->addChild(new_node);
  new_node->parent = parent_node;
  return;
}

int FileSystem::mknod(const char* path, mode_t mode) {
  if (!write_support) return ENOTSUP;

  FileNode* node;
  try {
    node = new FileNode(path, NULL, FileNode::FILE_NODE);
    node->buffer = new Buffer; //není třeba uzamykat neboť ještě není připojen
  }
  catch (bad_alloc&) {
    delete node;
    return ENOMEM;
  }
  node->file_info.st_mode = mode;
  node->file_info.st_mode |= S_IFREG;
  node->changed = true;

  try {
    append(node);
  }
  catch (FileSystem::AlreadyExists&) {
    delete node;
    return EEXIST;
  }
  changed = true;
  return 0;
}

int FileSystem::create(const char* path, mode_t mode, FileNode** new_node) {
  if (!write_support) return ENOTSUP;

  try {
    *new_node = new FileNode(path, NULL, FileNode::FILE_NODE);
    (*new_node)->buffer = new Buffer; //není třeba uzamykat neboť ještě není připojen
  }
  catch (bad_alloc&) {
    delete *new_node;
    return ENOMEM;
  }
  (*new_node)->file_info.st_mode = mode;
  (*new_node)->file_info.st_mode |= S_IFREG;

  // není třeba otevírat - Buffer je "otevřen"
  (*new_node)->ref_cnt = 1;
  (*new_node)->changed = true;

  try {
    append(*new_node);
  }
  catch (FileSystem::AlreadyExists&) {
    delete *new_node;
    return EEXIST;
  }
  changed = true;

  return 0;
}

int FileSystem::mkdir(const char* path, mode_t mode) {
  if (!write_support) return ENOTSUP;

  FileNode* node;
  try {
    node = new FileNode(path, NULL, FileNode::DIR_NODE);
  }
  catch (bad_alloc&) {
    return ENOMEM;
  }
  node->file_info.st_mode = mode;
  node->file_info.st_mode |= S_IFDIR;
  node->changed = true;

  try {
    append(node);
  }
  catch (FileSystem::AlreadyExists&) {
    delete node;
    return EEXIST;
  }
  changed = true;
  return 0;
}

int FileSystem::rename(FileNode* node, const char* new_pathname) {
  if (!write_support) return ENOTSUP;

  /* Odebrání souboru pod stávajícím jménem */
  if (!take(node)) return ENOENT;

  /* Pokud soubor s názvem new_pathname již existuje, je odstraněn */
  pthread_mutex_lock(&fmap_mux);
    FileMap::iterator it = file_map.find(new_pathname);

  if (it != file_map.end()) remove(it->second);
  pthread_mutex_unlock(&fmap_mux);

  /* Původní jméno potřeba uchovat pouze pokud se soubor nachází v archivu
   * a pokud ještě nebyl přejmenován.
   */
  if (node->data && node->original_pathname == NULL)
    node->original_pathname = node->pathname;
  else
    free(node->pathname);

  node->pathname = strdup(new_pathname);

  /* Vykousnu jméno souboru */
  node->name_ptr = node->pathname;
  while (*node->name_ptr) ++node->name_ptr;
  while (node->name_ptr != node->pathname && *node->name_ptr != '/') --node->name_ptr;

  if (node->name_ptr != node->pathname) ++node->name_ptr;

  append(node);

  if (node->type == FileNode::DIR_NODE) {
    for (FileList::iterator it = node->children.begin(); it != node->children.end(); ++it) {
      repath(*it, node->pathname);
    }
  }

  /* Virtuální filesystém v archivu se změnil */
  changed = true;

  return 0;
}

void FileSystem::repath(FileNode* node, const char* path) {
  char new_pathname[PATH_MAX];

  pthread_mutex_lock(&fmap_mux);
    file_map.erase(node->pathname);
  pthread_mutex_unlock(&fmap_mux);

  strcpy(new_pathname, path);
  strcat(new_pathname, "/");
  strcat(new_pathname, node->name_ptr);

  /* Uzel se nachází v archivu a dosud nebyl přejmenován */
  if (node->data != NULL && node->original_pathname == NULL)
    node->original_pathname = node->pathname;
  else
    free(node->pathname);

  node->pathname = strdup(new_pathname);
  node->name_ptr = node->pathname;
  while (*node->name_ptr) ++node->name_ptr;
  while (node->name_ptr != node->pathname && *node->name_ptr != '/') --node->name_ptr;

  if (node->name_ptr != node->pathname) ++node->name_ptr;

  pthread_mutex_lock(&fmap_mux);
    file_map[node->pathname] = node;
  pthread_mutex_unlock(&fmap_mux);

  if (node->type == FileNode::DIR_NODE) {
    for (FileList::iterator it = node->children.begin(); it != node->children.end(); ++it)
      repath(*it, node->pathname);
  }
}

int FileSystem::open(FileNode* node, int flags) {
  ++node->ref_cnt;

  if (node->ref_cnt == 1 && node->buffer == NULL)
    driver->open(node);

  if (flags & O_WRONLY || flags & O_RDWR) {
    if (!write_support) return ENOTSUP;
    if (!node->buffer) {
      pthread_rwlock_wrlock(&(node->lock));
      try {
        node->buffer = new Buffer(node->getSize());
      }
      catch (bad_alloc) {
        pthread_rwlock_unlock(&(node->lock));
        return ENOMEM;
      }
      fillInBuffer(node);
      pthread_rwlock_unlock(&(node->lock));
    }
  }

  return 0;
}

int FileSystem::read(FileNode* node, char* buffer, size_t bytes, off_t offset) {
  if (bytes == 0) return 0;

  if (node->buffer) {
    pthread_rwlock_rdlock(&(node->lock));
      bytes = node->buffer->read(buffer, bytes, offset);
    pthread_rwlock_unlock(&(node->lock));

    return bytes;
  }
  else return (driver->read(node, buffer, bytes, offset));
}

int FileSystem::write(FileNode* node, const char* buffer,
                          size_t length, off_t offset) {
  if (node->buffer == NULL) return EBADF;

  int written;
  pthread_rwlock_wrlock(&(node->lock));
  try {
    written = node->buffer->write(buffer, length, offset);
  }
  catch (bad_alloc&) {
    pthread_rwlock_unlock(&(node->lock));
    return -ENOMEM;
  }
  node->setSize(written+offset);
  node->changed = true;
  pthread_rwlock_unlock(&(node->lock));

  changed = true; //FileSystem has changed
  return written;
}

FileList* FileSystem::readDir(FileNode* node) {
  return &(node->children);
}

int FileSystem::truncate(FileNode* node, ssize_t size) {
  if (!write_support) return ENOTSUP;

  pthread_rwlock_wrlock(&(node->lock));
  if (node->buffer) node->buffer->truncate(size);
  else {
    try {
      node->buffer = new Buffer(size);
      if (size > 0)
        fillInBuffer(node, size);
    }
    catch (std::bad_alloc()) {
      pthread_rwlock_unlock(&(node->lock));
      return ENOMEM;
    }
  }
  node->setSize(size);
  node->changed = true;
  pthread_rwlock_unlock(&(node->lock));

  changed = true;
  return 0;
}

int FileSystem::remove(FileNode* node) {
  if (!write_support) return ENOTSUP;

  // Odstraň všechny synovské uzly
  if (node->type == FileNode::DIR_NODE) {
    while (!node->children.empty()) {
      remove(node->children.front());
    }
  }

  take(node);

  // Smazaný soubor se nachází v archivu
  if (node->data != NULL)
    removed_nodes.push_back(node);
  else
    delete node;

  changed = true;
  return 0;
}

bool FileSystem::isPathSearchable(FileNode* node, uid_t uid, gid_t gid) {
  mode_t mode;

  if (uid == FileNode::uid)      mode = S_IXUSR;
  else if (gid == FileNode::gid) mode = S_IXGRP;
  else                           mode = S_IXOTH;

  FileNode* dir = node->parent;
  while (dir != NULL) {
    if (dir->file_info.st_mode & mode) dir = dir->parent;
    else return false;
  }
  return true;
}

int FileSystem::parentAccess(const char* path, int mask, uid_t uid, gid_t gid) {
  char* parent = strdup(path);
  char* ptr = parent;
  while (*ptr) ++ptr;
  while (ptr != parent && *ptr != '/') --ptr;

  /* For root node all permissions are granted */
  if (ptr == parent) {
    free(parent);
    return 0;
  }

  *ptr = '\0';

  FileNode* node = find(parent);
  free(parent);

  if (node == NULL) return ENOENT;
  return access(node, mask, uid, gid);
}

int FileSystem::access(FileNode* node, int mask, uid_t uid, gid_t gid) {
  if (mask == F_OK) return 0;

  /* Privilegovaní uživatelé mají spešl trítment */
  if (uid == 0 || gid == 0) {
    /* Superuser má právo spouštět obyčejné soubory pouze pokud je právo
     * spuštění definováno pro uživatele, skupinu nebo všechny */
    if ((mask & X_OK) && node->type == FileNode::FILE_NODE) {
      if (node->file_info.st_mode & S_IXUSR ||
          node->file_info.st_mode & S_IXGRP ||
          node->file_info.st_mode & S_IXOTH)
        return 0;
      else
        return EACCES;
    } else
      return 0;
  }

  if (!isPathSearchable(node, uid, gid)) return EACCES;

  mode_t mode = 0;
  if (uid == FileNode::uid) {
    if (mask & R_OK) mode += S_IRUSR;
    if (mask & W_OK) mode += S_IWUSR;
    if (mask & X_OK) mode += S_IXUSR;
  } else if (gid == FileNode::gid) {
    if (mask & R_OK) mode += S_IRGRP;
    if (mask & W_OK) mode += S_IWGRP;
    if (mask & X_OK) mode += S_IXGRP;
  } else {
    if (mask & R_OK) mode += S_IROTH;
    if (mask & W_OK) mode += S_IWOTH;
    if (mask & X_OK) mode += S_IXOTH;
  }

  if ((mode & node->file_info.st_mode) == mode)
    return 0;
  else
    return EACCES;
}

int FileSystem::utimens(FileNode* node, const struct timespec times[2]) {
  if (!write_support) return ENOTSUP;

  node->file_info.st_atime = times[0].tv_sec;
  node->file_info.st_mtime = times[1].tv_sec;
  //TODO: node->changed = true;
  return 0;
}

void FileSystem::fillInBuffer(FileNode* node, ssize_t size) {
  unsigned bytes_to_read;
  if (size == 0) bytes_to_read = node->getSize();
  else bytes_to_read = size;

  //TODO: make sparse file

  unsigned bytes_read = 0;
  unsigned read_offset = 0;
  driver->open(node);
  char tmp_buf[Buffer::BLOCK_SIZE];
  while (read_offset < bytes_to_read) {
    bytes_read = driver->read(node, tmp_buf, Buffer::BLOCK_SIZE, read_offset);
    node->buffer->write(tmp_buf, bytes_read, read_offset);
    read_offset += bytes_read;
  }
  driver->close(node);

  node->file_info.st_mtime = time(NULL);
}

void FileSystem::close(FileNode* node) {
  --node->ref_cnt;

  if (node->changed) return;

  if (node->ref_cnt == 0)
    driver->close(node);
}

struct stat* FileSystem::getAttr(FileNode* node) {
  return &(node->file_info);
}

vector<FileNode*> FileSystem::getFileNames() const {
  vector<FileNode*> nodes;
  FileMap::const_iterator it;
  for (it = file_map.begin(); it != file_map.end(); ++it) {
    nodes.push_back((*it).second);
  }
  return nodes;
}

/**
 * Funkce inicializující strukturu statvfs
 *
 *  struct statvfs {
 *    unsigned long  f_bsize;    file system block size
 *    unsigned long  f_frsize;   fragment size
 *    fsblkcnt_t     f_blocks;   size of fs in f_frsize units
 *    fsblkcnt_t     f_bfree;    # free blocks
 *    fsblkcnt_t     f_bavail;   # free blocks for unprivileged users
 *    fsfilcnt_t     f_files;    # inodes
 *    fsfilcnt_t     f_ffree;    # free inodes
 *    fsfilcnt_t     f_favail;   # free inodes for unprivileged users
 *    unsigned long  f_fsid;     file system ID
 *    unsigned long  f_flag;     mount flags
 *    unsigned long  f_namemax;  maximum filename length
 *  };
 *
 * The 'f_frsize', 'f_favail', 'f_fsid' and 'f_flag' fields are ignored
 */
void FileSystem::initStatvfs() {
  memset(&archive_statvfs, 0, sizeof(struct statvfs));

  struct statvfs buf;

  if (fstatvfs(archive_file, &buf) != 0) return;

  // Volné místo zjistíme podle "underlying" filesystému
  archive_statvfs.f_bavail = archive_statvfs.f_bfree = buf.f_frsize * buf.f_bavail;

  archive_statvfs.f_bsize = 1;

  // Velikost vfs v blocích
  archive_statvfs.f_blocks = buf.f_bavail;

  archive_statvfs.f_files = file_map.size() - 1; // - root node
  archive_statvfs.f_namemax = 255;

  return;
}

bool FileSystem::releaseUnchanged() {
  FileNode* node;
  bool released = false;
  pthread_mutex_lock(&fmap_mux);
  for (FileMap::iterator it = file_map.begin(); it != file_map.end(); ++it) {
    node = (*it).second;
    if (node->changed) continue;

    if (node->buffer) delete node->buffer;
    node->buffer = NULL;
    released = true;
  }
  pthread_mutex_unlock(&fmap_mux);
  return released;
}

void FileSystem::removeTrash() {
  FileList root_files = root_node->children;
  FileNode* node;
  for (FileList::iterator file = root_files.begin(); file != root_files.end(); ++file) {
    node = *file;
    if (strncmp(node->name_ptr, ".Trash", 6) == 0) {
      remove(node);
    }
  }
}

void printStat(struct stat* info) {
    cout << "File Size: \t\t"     << info->st_size << " bytes" << endl;
    cout << "Number of Links: \t" << info->st_nlink << endl;
    cout << "File inode: \t\t"    << info->st_ino << endl;
    cout << "UID, GID:\t\t"       << info->st_uid << ", " << info->st_gid << endl;
    cout << "a/m/c time:\t\t"     << info->st_atime << ", "
                                  << info->st_mtime << ", "
                                  << info->st_ctime << endl;
    cout << "Blocks: \t\t"        << info->st_blocks << " of " << info->st_blksize << endl;

    cout << "File Permissions: \t";
    cout << (S_ISDIR(info->st_mode) ? "d" : "-");
    cout << (info->st_mode & S_IRUSR ? "r" : "-");
    cout << (info->st_mode & S_IWUSR ? "w" : "-");
    cout << (info->st_mode & S_IXUSR ? "x" : "-");
    cout << (info->st_mode & S_IRGRP ? "r" : "-");
    cout << (info->st_mode & S_IWGRP ? "w" : "-");
    cout << (info->st_mode & S_IXGRP ? "x" : "-");
    cout << (info->st_mode & S_IROTH ? "r" : "-");
    cout << (info->st_mode & S_IWOTH ? "w" : "-");
    cout << (info->st_mode & S_IXOTH ? "x" : "-") << endl;

    cout << "The file " << (S_ISLNK(info->st_mode) ? "is" : "is not") <<
    " a symlink" << endl;
}

