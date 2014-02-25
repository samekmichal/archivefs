/* Project:  ArchiveFS
 * Author:   Michal SAMEK
 * Email:    xsamek01@fit.vutbr.cz
 * Desc:     Source file implementing class for nodes (files) in virtual file system
 * Modified: 04/2012
 */

#include <cstring>
#include <cstdlib>
#include <cassert>
#include <ctime>
#include <algorithm>

#include "filenode.hpp"
#include "archivedriver.hpp"

uid_t FileNode::uid = geteuid();
gid_t FileNode::gid = getegid();


/* Konstruktor tridy FileNode
 * Sam si vykousne jmeno z predane cesty skrze _pathname.
 * _pathname je absolutni cesta k souboru uvnitr archivu, která nezačíná slashem
 * Pres parametr _filedata se predava naalokovane misto pro konkretni tridu:
 * napr. ZipFileData, IsoFileData */
FileNode::FileNode(const char* _pathname, FileData* _data, enum NodeType _type)
  : type(_type),
    pathname(NULL),
    name_ptr(NULL),
    original_pathname(NULL),
    buffer(NULL),
    ref_cnt(0),
    changed(false),
    parent(NULL),
    data(_data) {

  memset(&file_info, 0, sizeof(struct stat));
  /* atributy st_dev, st_blksize, st_ino jsou ignorovány */
  file_info.st_uid = uid;
  file_info.st_gid = gid;
  file_info.st_blksize = STANDART_BLOCK_SIZE;

  if (_data == NULL)
    file_info.st_atime = file_info.st_ctime = file_info.st_mtime = time(NULL);

  pthread_rwlock_init(&lock, NULL);

  switch (_type) {
    case ROOT_NODE:
      file_info.st_size = STANDART_BLOCK_SIZE;
      file_info.st_blocks = 8;
      file_info.st_mode = S_IFDIR | 0755;
      file_info.st_nlink = 2;
      return;

    case DIR_NODE:
      file_info.st_size = STANDART_BLOCK_SIZE;
      file_info.st_blocks = 8;
      file_info.st_mode = S_IFDIR | 0755;
      file_info.st_nlink = 2;
      break;

    case FILE_NODE:
      file_info.st_size = 0;
      file_info.st_blocks = 0;
      file_info.st_mode = S_IFREG | 0644;
      file_info.st_nlink = 1;
      break;
  }

  pathname = strdup(_pathname);
  if (pathname == NULL) {
    pthread_rwlock_destroy(&lock);
    throw bad_alloc();
  }

  /* Ukazatel name_ptr presuneme na konec retezce pathname. */
  name_ptr = pathname;
  while (*name_ptr) ++name_ptr;

  /* Hledam posledni slash od konce retezce.
   * Jestli dojdu az na zacatek cesty, nachazi se tento soubor v korenovem
   * adresari archivu. */
  while (name_ptr != pathname && *name_ptr != '/') --name_ptr;

  if (name_ptr != pathname)
    name_ptr++;

  return;
}

FileNode::~FileNode() {
  free(pathname);
  free(original_pathname);
  pthread_rwlock_destroy(&lock);

  delete buffer;
  delete data;
}

void FileNode::addChild(FileNode* node) {
  if (this->type == FILE_NODE) return;
  this->children.push_back(node);
  if (node->type == DIR_NODE || node->type == ROOT_NODE)
    this->file_info.st_nlink++;
  return;
}

void FileNode::removeChild(FileNode* node) {
  FileList::iterator it = find(children.begin(), children.end(), node);
  if (it !=  children.end()) {
    children.erase(it);
    if (node->type == DIR_NODE)
      this->file_info.st_nlink--;
  }
}

void FileNode::setSize(offset_t size) {
  file_info.st_size = size;
  file_info.st_blocks = (size + STANDART_BLOCK_SIZE -1)/STANDART_BLOCK_SIZE;
}

offset_t FileNode::getSize() {
  return this->file_info.st_size;
}

ostream& operator<< (ostream& stream, FileNode& node) {
  stream << "\tpathname: " << ((node.pathname != NULL) ? node.pathname : "NULL") << endl;

  stream << "\tstat info: "  << endl;
  stream << "\t\tsize: "     << node.file_info.st_size << " bytes" << endl;
  stream << "\t\tlinks: "    << node.file_info.st_nlink << endl;
  stream << "\t\tinode: "    << node.file_info.st_ino << endl;

  stream << "\tpermissions: ";
  stream << (S_ISDIR(node.file_info.st_mode) ? "d" : "-");
  stream << (node.file_info.st_mode & S_IRUSR ? "r" : "-");
  stream << (node.file_info.st_mode & S_IWUSR ? "w" : "-");
  stream << (node.file_info.st_mode & S_IXUSR ? "x" : "-");
  stream << (node.file_info.st_mode & S_IRGRP ? "r" : "-");
  stream << (node.file_info.st_mode & S_IWGRP ? "w" : "-");
  stream << (node.file_info.st_mode & S_IXGRP ? "x" : "-");
  stream << (node.file_info.st_mode & S_IROTH ? "r" : "-");
  stream << (node.file_info.st_mode & S_IWOTH ? "w" : "-");
  stream << (node.file_info.st_mode & S_IXOTH ? "x" : "-") << endl;

  stream << "\tsymlink:  " << (S_ISLNK(node.file_info.st_mode) ? "yes" : "no") << endl;

  stream << "\tchildren: " << endl;
  for (FileList::const_iterator it = node.children.begin();
       it != node.children.end();
       ++it) {
    stream << "\t\t" << (*it)->name_ptr << endl;
  }

  stream << endl;

  return stream;
}

