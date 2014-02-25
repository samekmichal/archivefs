/* Project:  ArchiveFS
 * Author:   Michal SAMEK
 * Email:    xsamek01@fit.vutbr.cz
 * Desc:     File implementing FUSE filesystem.
 * Modified: 04/2012
 */

#include <iostream>
#include <fstream>
#include <cstring>
#include <cstdlib>
#include <climits>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>

#include "archivefs.hpp"

bool ArchiveDriver::respect_rights = false;
bool ArchiveDriver::keep_original  = false;

struct fuse_operations fuse_oper;

static int process_arg(void* data, const char* arg, int key,
                       struct fuse_args* outargs) {

  FusePrivate* fuse_data = reinterpret_cast<FusePrivate*>(data);

  /* Pocitadlo jiz zpracovanych parametru */
  static short param_cnt = 0;

  switch (key) {
    case KEY_HELP:
      printHelp();
      fuse_opt_add_arg(outargs, "-ho");
      fuse_main(outargs->argc, outargs->argv, &fuse_oper, NULL);
      delete fuse_data;
      exit(0);
      break;

    case KEY_SUPPORTED:
      path_to_drivers = realpath(fuse_data->drivers_path, NULL);
      LOAD_STANDARD_DRIVERS();
      PRINT_DRIVERS_SUPPORT();
      delete fuse_data;
      exit(0);
      break;

    case KEY_VERSION:
      cout << endl << "ArchiveFS version " << ARCHIVE_FS_VERSION << endl << endl;
      fuse_opt_add_arg(outargs, "--version");
      fuse_main(outargs->argc, outargs->argv, &fuse_oper, NULL);
      delete fuse_data;
      exit(0);
      break;

    case KEY_VERBOSE:
      fuse_opt_add_arg(outargs, "-d");
      return FUSE_DISCARD;

    case FUSE_OPT_KEY_NONOPT:
      ++param_cnt;
      switch (param_cnt) {
        case 1:
          /* Zdrojový soubor pro filesystém, buď archiv nebo adresář s archivy */
          fuse_data->mounted = const_cast<char*>(arg);
          return FUSE_DISCARD;

        case 2:
          /* Mountpoint */
          fuse_data->mountpoint = strdup(arg);
          return FUSE_DISCARD;

        default:
          if (fuse_data->load_driver) {
            if (!LOAD_DRIVER(arg)) {
              cerr << "Error: " << arg << " driver not loaded" << endl;
            }
            return FUSE_DISCARD;
          }
          cerr << "Unknown parameter " << arg << endl;
          return FUSE_ERROR;
      }

    default:
      return FUSE_KEEP;
  }
}


/* main()
 *****************************************************************************/

int main(int argc, char* argv[]) {
  int retcode;

  if (argc < 2) {
    printHelp();
    return -1;
  }

  if ((getuid() == 0) || (geteuid() == 0)) {
    cout << RUN_AS_ROOT_WARN << endl;
  }

  FusePrivate* fuse_data = new FusePrivate(argc, argv);

  if (fuse_opt_parse(&fuse_data->args, fuse_data, fuse_opts, process_arg)) {
    exit(-1);
  }

  if (!fuse_data->mounted || !fuse_data->mountpoint) {
    cout << "usage: afs <source_file> <mountpoint> [OPTIONS]" << endl;
    fuse_data->mounted = NULL;
    delete fuse_data;
    exit(-1);
  }

  fuse_opt_add_arg(&fuse_data->args, fuse_data->mountpoint);

  if (!initialize(fuse_data)) {
    delete fuse_data;
    return -1;
  }

  memset(&fuse_oper, 0, sizeof(struct fuse_operations));
  fuse_oper.init       = archivefs_init;
  fuse_oper.destroy    = archivefs_destroy;
  fuse_oper.getattr    = archivefs_getattr;
  fuse_oper.open       = archivefs_open;
  fuse_oper.read       = archivefs_read;
  fuse_oper.release    = archivefs_release;
  fuse_oper.opendir    = archivefs_opendir;
  fuse_oper.readdir    = archivefs_readdir;
  fuse_oper.releasedir = archivefs_releasedir;
  fuse_oper.statfs     = archivefs_statfs;
  fuse_oper.access     = archivefs_access;

  if (!fuse_data->read_only) {
    fuse_oper.mknod      = archivefs_mknod;
    fuse_oper.mkdir      = archivefs_mkdir;
    fuse_oper.create     = archivefs_create;
    fuse_oper.rename     = archivefs_rename;
    fuse_oper.write      = archivefs_write;
    fuse_oper.truncate   = archivefs_truncate;
    fuse_oper.unlink     = archivefs_unlink;
    fuse_oper.rmdir      = archivefs_rmdir;
    fuse_oper.chmod      = archivefs_chmod;
    fuse_oper.utimens    = archivefs_utimens;
  }

  retcode = fuse_main(fuse_data->args.argc, fuse_data->args.argv, &fuse_oper, fuse_data);

  if (retcode != 0) {
    delete fuse_data;
  }

  return retcode;
}

void printHelp() {
  cout << HELP_TEXT << endl << endl;
}

bool controlArgs(FusePrivate* data) {
  if (data->create_archive && data->mode == FusePrivate::FOLDER_MOUNTED)
    return false;

  if (data->create_archive && data->keep_original)
    return false;

  if (data->respect_rights && data->create_archive)
    return false;

  return true;
}

bool initialize(FusePrivate* data) {
  int ret;
  ArchiveType* archive_type = NULL;

  if (data->drivers_path)
    path_to_drivers = realpath(data->drivers_path, NULL);

  LOAD_STANDARD_DRIVERS();
  if (drivers->empty()) return false;

  FileSystem::setBufferLimit(data->buffer_limit);
  if (data->keep_trash)     FileSystem::keep_trash = true;
  if (data->respect_rights) ArchiveDriver::respect_rights = true;
  if (data->keep_original)  ArchiveDriver::keep_original = true;

  /* První část inicializace */
  if (data->create_archive) {
    data->mode = FusePrivate::ARCHIVE_MOUNTED;

    char* path = (char*)malloc(PATH_MAX);
    memset(path, 0, PATH_MAX);

    getcwd(path, PATH_MAX);
    strcat(path, "/");
    strcat(path, data->mounted);
    data->mounted = path;

    /* soubor již existuje */
    if (access(path, F_OK) == 0) {
      cerr << "Archive file with specified name already exist" << endl;
      return false;
    }

    /* Najdeme koncovku a ovladač */
    const char* ext = findFileExt(data->mounted, NULL);
    archive_type = TYPE_BY_EXT(ext);
    if (archive_type == NULL) {
      cerr << "Error: this type of archive files is not supported" << endl;
      /* Při destrukci FusePrivate bude data->mounted uvolňován */
      data->mounted = NULL;
      return false;
    }

    if (!archive_type->write_support) {
      /* Při destrukci FusePrivate bude data->mounted uvolňován */
      data->mounted = NULL;
      cerr << "Write support for this type of archive is not implemented, sorry" << endl;
      return false;
    }
  } else {
    data->mounted = realpath(data->mounted, NULL);
    ret = errno;
    if (data->mounted == NULL) {
      cerr << "Error: " << strerror(ret) << endl;
      return false;
    }

    ret = setMountMode(data);
    if (ret == -1) {
      cerr << "Unsupported type of source file, sorry" << endl;
      return false;
    } else if (ret > 0) {
      cerr << "Error: " << strerror(errno) << endl;
      return false;
    }
  }

  /* Kontrola kombinace parametrů */
  if (!controlArgs(data)) {
    cerr << "Invalid combination of parameters" << endl;
    return false;
  }

  /* Druhá část inicializace - budování FileSystémů ze zdrojů */
  FileSystem* fs = NULL;
  if (data->mode == FusePrivate::ARCHIVE_MOUNTED) {
    /* Pokud se vytváří nový archiv, ovladač je již načten.
     * Pokud ne, je třeba jej načíst.
     */
    if (!data->create_archive) {
      /* data->mounted už je realpath-ed */
      archive_type = GET_TYPE(data->mounted);
      if (archive_type == NULL) {
        cerr << "Error: Could not load driver for " << data->mounted << endl;
        return false;
      }
    }

    try {
      fs = new FileSystem(data->mounted, data->create_archive, archive_type);
    }
    catch (ArchiveDriver::ArchiveError&) {
      cerr << "Error: Failed to open/process archive file" << endl;
      return false;
    }

    data->filesystems->insert(fs);

  } else {
    DIR* dir;
    struct dirent* file;
    struct stat info;

    char filename[PATH_MAX];
    char* filename_ptr;
    unsigned mounted_len = strlen(data->mounted);
    strcpy(filename, data->mounted);
    filename[mounted_len] = '/';
    filename_ptr = filename + mounted_len + 1;

    /* Projdu celý připojený adresář a pokud obsahuje archivy podporovaných typů
     * vytvořím jejich filesystémy */
    dir = opendir(data->mounted);
    while ((file = readdir(dir)) != NULL) {
      if (strcmp(file->d_name, ".") == 0 || strcmp(file->d_name, "..") == 0)
        continue;

      archive_type = NULL;

      // Získání celé cesty k archivu
      strcpy(filename_ptr, file->d_name);

      if (stat(filename, &info) != 0) continue;

      if (S_ISREG(info.st_mode) || S_ISLNK(info.st_mode)) {
        archive_type = GET_TYPE(filename);
        if (archive_type != NULL) {
          try {
            fs = new FileSystem(filename, false, archive_type);
          }
          catch (...) {
            continue;
          }

          data->filesystems->insert(fs);
        }
      }
    }

    closedir(dir);
  }

//   cout << "FILESYSTEM IS INITIALIZED" << endl;

  return true;
}


/* setMountMode()
 *  nastavuje v jakém modu bude program pracovat, což rozhodne podle toho,
 *  je-li nastaven jako zdrojový soubor archív nebo adresář.
 *  stat() - je možno připojovat i symlinky
 *
 *  v případě chyby vrací -1
 */
int setMountMode(FusePrivate* fuse_data) {
  struct stat info;
  memset(&info, 0, sizeof(struct stat));
  errno = 0;
  if (stat(fuse_data->mounted, &info) != 0) {
    return errno;
  }

  if (S_ISREG(info.st_mode)) {
    fuse_data->mode = FusePrivate::ARCHIVE_MOUNTED;
  } else if (S_ISDIR(info.st_mode)) {
    fuse_data->mode = FusePrivate::FOLDER_MOUNTED;
  } else
    return -1;

  return 0;
}


/* fullpath()
 *  provede katenaci řetězce předaného v path s cestou ke zdrojovému
 *  souboru filesystému - první parametr při spuštění
 */
void fullpath(char* fpath, const char* path) {
  FusePrivate* fuse_data  = PRIVATE_DATA;

  strcpy(fpath, fuse_data->mounted);
  strcat(fpath, path);
}


/* parsePathName()
 *  rozdělí cestu v path na cestu k fyzickému archivu na disku a souboru
 *  uvnitř tohoto archivu
 *
 *  za archiv může být označen jakýkoliv regulérní soubor na disku
 *  --> první regulérní soubor nacházející se v cestě path
 *
 *  pokud se v path nenachází žáden regulérní soubor vrací false, jinak true
 */
bool parsePathName(char* path, char** file) {
  bool archive_found = false;

  *file = path+1;//přeskočím první slash

  struct stat info;
  memset(&info, 0, sizeof(struct stat));

  /* Dokud nenajdu archív nebo dokud se nedostanu na konec řetězce fpath provádím:
   * 1. najdu ve fpath první/další slash
   * 2. nahradím ho nulovým bytem
   * 3a.pokud fpath obsahuje cestu k adresáři, dáme slash zpátky --> 1
   * 3b.pokud se nejedna o cestu k adresáři, našli jsme archiv
   */
  char c;
  do {
    if (**file == '\0') break;

    while (**file != '\0' && **file != '/') ++*file;

    c = **file;
    **file = '\0';

    if (stat(path, &info) == 0) {
      if (S_ISDIR(info.st_mode)) {
        // jedná se o adresář a jsme již na konci řetězce - break
        if (c == '\0') break;
        **file = c;
        ++*file;
      } else {
        archive_found = true;
        break;
      }
    } else {
      break;
    }
  } while(!archive_found);

  if (!archive_found) return false;


  ++(*file); // posun an první platný znak cesty k souboru uvnitř archivu

  /* Dotazován kořenový soubor např. /archivy/try.zip
   * Poslední zpracovávaný znak byl buď null-byte nebo slash, za kterým už
   * nic není
   */
  if (c == '\0' || (c == '/' && **file == '\0'))
    *file = NULL;

  return true;
}

/* getFile()
 *  naplní ukazatele na FileSystem a FileNode, patřící k souboru, předanému
 *  přes fpath
 *
 *  pokud filesystém k danému archivu ještě není vytvořen, zkusí jej vytvořit
 *
 *  v pokud se patřičné objekty nepodaří nalézt, vrací false
 */
bool getFile(char* fpath, FileSystem** fs, FileNode** node) {
  FusePrivate* fuse_data = PRIVATE_DATA;
  ArchiveType* archive_type = NULL;

  char* fpath_dup = strdup(fpath);
  char* file = NULL;

  if (!parsePathName(fpath_dup, &file)) {
    free((void*)fpath_dup);
    return false;
  }

  *fs = fuse_data->filesystems->find(fpath_dup);
  if (*fs == NULL) {

    /* Pokud nebyl odpovídající FileSystem nalezen, je možné, že
     * ještě nebyl vytvořen ==> zkusím jej vytvořit.
     * Pokud bude vyvolána vyjímka SourceFileError, znamená to, že
     * FileSystem nemohl být vytvořen - není třeba jej uvolňovat.
     * Pokud je zachycena jakákoliv jiná vyjímka, musel ji vyvolat ovladač
     * patříčného archivu, přičemž objekt již byl vytvořen, proto jej
     * třeba uvolnit.
     */
    if ((archive_type = GET_TYPE(fpath_dup)) != NULL) {
      try {
        *fs = new FileSystem(fpath_dup, false, archive_type);
      }
      catch (ArchiveDriver::ArchiveError&) {
        free((void*)fpath_dup);
        return false;
      }

      if (fuse_data->keep_trash) FileSystem::keep_trash = true;

      fuse_data->filesystems->insert(*fs);
    } else {
      free((void*)fpath_dup);
      *fs = NULL;
      return false;
    }
  }

  // Neni potřeba vyhledavat soubor
  if (node == NULL) {
    free((void*)fpath_dup);
    return true;
  }

  // Pokud je cesta prázdná - jedná se o kořen filesystému
  if (file == NULL)
    (*node) = (*fs)->getRoot();
  else
    (*node) = (*fs)->find(file);

  free((void*)fpath_dup);
  if (*node == NULL) {
    return false;
  } else {
    return true;
  }
}


/* convertFlagsToDir()
 *  struktuře předané parametrem nastaví práva a typ souboru = adresář
 */
void convertFlagsToDir(struct stat* info) {
  info->st_mode &= !S_IFREG;  //nulování bitu pro REG FILE
  info->st_mode &= !S_IFLNK;  //a rovnou i pro symlink
  info->st_mode |= S_IFDIR;   //nastavení bitu pro adresáře

  //přenastavení práv
  info->st_mode |= (S_IRWXU|S_IRWXG|S_IROTH|S_IXOTH);
}

void print_err(const char* operation, const char* path, int err) {
  cerr << "FAILED " << operation << ": " << path << "\n\t" << strerror(err)
       << endl << endl;
}

/* FUSE OPERATIONS
 *****************************************************************************/
void* archivefs_init(struct fuse_conn_info* conn) {
  (void)conn;
  FusePrivate* fuse_data = PRIVATE_DATA;
  return ((void*)fuse_data);
}

void archivefs_destroy(void* private_data) {
  delete reinterpret_cast<FusePrivate*>(private_data);
  cout << "\tbye..."<< endl << endl;
  return;
}

int archivefs_getattr(const char* path, struct stat* info) {
  char fpath[PATH_MAX];
  fullpath(fpath, path);

  FileSystem* fs = NULL;
  FileNode* node;
  int ret;

  if (!getFile(fpath, &fs, &node)) {
    /* fs byl nalezen, ale soubor ne */
    if (fs != NULL) {
      print_err("GETATTR", path, ENOENT);
      return -ENOENT;
    }

    if ((ret = stat(fpath, info)) != 0) {
      ret = errno;
      print_err("GETATTR", path, ret);
    }
    return -ret;
  }

  struct stat* node_info = fs->getAttr(node);
  memcpy(info, node_info, sizeof(struct stat));

//   printStat(info);

  return 0;
}

int archivefs_statfs(const char *path, struct statvfs *info) {
  char fpath[PATH_MAX];
  fullpath(fpath, path);

  if (statvfs(fpath, info) == 0) {
    return 0;
  }

  FileSystem* fs;
  if (!getFile(fpath, &fs, NULL)) {
    print_err("STATFS", path, ENOENT);
    return -ENOENT;
  }

  memcpy(info, &(fs->archive_statvfs), sizeof(struct statvfs));
  return 0;
}

int archivefs_mknod(const char* path, mode_t mode, dev_t dev) {
  char fpath[PATH_MAX];
  fullpath(fpath, path);

  FileSystem* fs;
  int ret;
  if (!getFile(fpath, &fs, NULL)) {
    ret = mknod(fpath, mode, dev);
    if (ret) {
      ret = errno;
      print_err("MKNOD", fpath, ret);
    }
    return -ret;
  }

  char* file;
  parsePathName(fpath, &file);

  struct fuse_context* context = fuse_get_context();
  if (fs->parentAccess(file, W_OK|X_OK, context->uid, context->gid))
    return -EACCES;


  ret = fs->mknod(file, mode);
  if (ret)
    print_err("MKNOD", path, ret);

  return -ret;
}

int archivefs_create(const char *path, mode_t mode, struct fuse_file_info *info) {
  char fpath[PATH_MAX];
  fullpath(fpath, path);

  FileSystem* fs;
  int ret;

  if (!getFile(fpath, &fs, NULL)) {
    ret = creat(fpath, mode);
    if (ret == -1) {
      ret = errno;
      print_err("CREATE", path, ret);
      return -ret;
    }
    info->fh = intptr_t(ret);
    return 0;
  }

  FileNode* node;
  char* file;
  parsePathName(fpath, &file);

  struct fuse_context* context = fuse_get_context();
  if (fs->parentAccess(file, W_OK|X_OK, context->uid, context->gid))
    return -EACCES;

  ret = fs->create(file, mode, &node);
  if (ret)
    print_err("CREATE", path, ret);

  info->fh = intptr_t(new FileHandle(fs, node));
  return -ret;
}

int archivefs_mkdir(const char* path, mode_t mode) {
  char fpath[PATH_MAX];
  fullpath(fpath, path);

  FileSystem* fs;
  int ret;

  if (getFile(fpath, &fs, NULL)) {
    char* file;
    parsePathName(fpath, &file);

    struct fuse_context* context = fuse_get_context();
    if (fs->parentAccess(file, W_OK|X_OK, context->uid, context->gid))
      return -EACCES;

    ret = fs->mkdir(file, mode);
    if (ret)
      print_err("MKDIR", path, ret);

    return -ret;
  }

  /* Pokud má vytvářený adresář známý suffix - vytvoříme místo adresáře
   * nový archív
   */
  ArchiveType* archive_type = NULL;
  const char* ext = findFileExt(fpath, NULL);
  if ((archive_type = TYPE_BY_EXT(ext)) != NULL) {
    FusePrivate* data = PRIVATE_DATA;
    try {
      fs = new FileSystem(fpath, true, archive_type);
    }
    catch (...) {
      goto end_if;
    }

    data->filesystems->insert(fs);
    cout << "New archive file was created" << endl;
    return 0;
  }
  end_if:

  ret = mkdir(fpath, mode);
  if (ret) {
    ret = errno;
    print_err("MKDIR", path, ret);
  }

  return -ret;
}

int archivefs_rename(const char* old_path, const char* new_path) {
  char fpath_old[PATH_MAX];
  fullpath(fpath_old, old_path);

  /* pro fyzické soubory je třeba opět rozvinou nové jméno, jinak by byl
   * soubor vytvořen v pracovním adresáři procesu
   */
  char fpath_new[PATH_MAX];
  fullpath(fpath_new, new_path);

  FileSystem* fs;
  FileNode* node;
  int ret;

  if (!getFile(fpath_old, &fs, &node)) {
    ret = rename(fpath_old, fpath_new);
    if (ret) {
      ret = errno;
      print_err("RENAME", old_path, ret);
    }
    return -ret;
  }

  /* Přejmenovává se celý archív */
  if (node->type == FileNode::ROOT_NODE) {
    ret = rename(fpath_old, fpath_new);
    if (ret) {
      ret = errno;
      print_err("RENAME", old_path, ret);
      return -ret;
    }
    FusePrivate* data = PRIVATE_DATA;

    data->filesystems->erase(fpath_old);
    free((void*)fs->archive_name);
    fs->archive_name = strdup(fpath_new);
    data->filesystems->insert(fs);
    return 0;
  }

  char* new_name;
  parsePathName(fpath_new, &new_name);
  ret = fs->rename(node, new_name);
  if (ret)
    print_err("RENAME", old_path, ret);

  return -ret;
}

int archivefs_open(const char* path, struct fuse_file_info* info) {
  char fpath[PATH_MAX];
  fullpath(fpath, path);

  /* Nejprve se pokusím otevřít soubor s cestou path jakoby byl přítomen
   * fyzicky na disku - použiju systémový open.
   * Pokud volání této funkce selže, jedná se o soubor uvnitř archivu.
   */

  int fd;
  if ((fd = open(fpath, info->flags)) != -1) {
    info->fh = intptr_t(fd);
    return 0;
  }

  FileSystem* fs;
  FileNode* node;

  if (!getFile(fpath, &fs, &node)) {
    print_err("OPEN", path, ENOENT);
    return -ENOENT;
  }

  int ret;
  struct fuse_context* context = fuse_get_context();
  if (info->flags & O_RDWR) {
    if (fs->access(node, R_OK|W_OK, context->uid, context->gid))
      return -EACCES;
  } else if (info->flags & O_WRONLY) {
    if (fs->access(node, W_OK, context->uid, context->gid))
      return -EACCES;
  } else {
    if (fs->access(node, R_OK, context->uid, context->gid))
      return -EACCES;
  }

  if ((ret = fs->open(node, info->flags)) < 0) {
    print_err("OPEN", path, ret);
    return -ret;
  }

  info->fh = intptr_t(new FileHandle(fs, node));

  return 0;
}

int archivefs_read(const char *path, char *buffer, size_t bufsize,
                   off_t offset, struct fuse_file_info *info) {
  (void)path;
  memset(buffer, 0, bufsize);

  FusePrivate* fuse_data = PRIVATE_DATA;
  FileHandle* fh = NULL;
  int ret = 0;

  /* Pokud je připojen archiv, tak se vždy bude pracovat pouze se soubory
   * uvnitř archivu...
   * Pokud je připojen adresář, tak vždy nejprve vyzkouším, jestli soubor
   * s cestou předanou v path existuje (fyzicky na disku), pokud ano, tak
   * použiju systémový read. Pokud neexistuje, jedná se opět o soubor v
   * archivu.
   */
  if (fuse_data->mode == FusePrivate::ARCHIVE_MOUNTED) {
    fh = reinterpret_cast<FileHandle*>(info->fh);
    ret = (fh->first->read(fh->second, buffer, bufsize, offset));
  } else {
    char fpath[PATH_MAX];
    fullpath(fpath, path);

    if (access(fpath, F_OK) == 0) {
      ret = pread(info->fh, buffer, bufsize, offset);
      if (ret < 0) {
        ret = errno;
        print_err("READ", path, ret);
        return -ret;
      }
    } else {
      fh = reinterpret_cast<FileHandle*>(info->fh);
      ret = (fh->first->read(fh->second, buffer, bufsize, offset));
      if (ret < 0)
        print_err("READ", path, ret);
    }
  }
  return ret;
}

int archivefs_write(const char* path, const char* buffer, size_t len,
                    off_t offset, struct fuse_file_info* info) {

  FusePrivate* fuse_data = PRIVATE_DATA;
  FileHandle* fh = NULL;
  int ret = 0;

  if (fuse_data->mode == FusePrivate::ARCHIVE_MOUNTED) {
    fh = reinterpret_cast<FileHandle*>(info->fh);
    ret = (fh->first->write(fh->second, buffer, len, offset));
  } else {
    char fpath[PATH_MAX];
    fullpath(fpath, path);

    if (access(fpath, F_OK) == 0) {
      ret = pwrite(info->fh, buffer, len, offset);
      if (ret < 0) {
        ret = errno;
        print_err("WRITE", path, ret);
        return -ret;
      }
    } else {
      fh = reinterpret_cast<FileHandle*>(info->fh);
      ret = (fh->first->write(fh->second, buffer, len, offset));
      if (ret < 0)
        print_err("WRITE", path, ret);
    }
  }

  return ret;
}

int archivefs_truncate(const char* path, off_t size) {
  char fpath[PATH_MAX];
  fullpath(fpath, path);

  FileSystem* fs;
  FileNode* node;
  int ret;

  if (!getFile(fpath, &fs, &node)) {
    ret = truncate(fpath, size);
    if (ret) {
      ret = errno;
    }
  } else {
    struct fuse_context* context = fuse_get_context();
    if (fs->access(node, W_OK, context->uid, context->gid))
      return -EACCES;
    ret = fs->truncate(node, size);
  }

  if (ret)
    print_err("TRUNCATE", path, ret);

  return -ret;
}

int archivefs_unlink(const char *path) {
  char fpath[PATH_MAX];
  fullpath(fpath, path);

  FileSystem* fs;
  FileNode* node;
  int ret;

  if (!getFile(fpath, &fs, &node)) {
    ret = unlink(fpath);
    if (ret) {
      ret = errno;
    }
  } else // není třeba testovat oprávnění, fuse volá access samo
    ret = fs->remove(node);

  if (ret)
    print_err("UNLINK", path, ret);

  return -ret;
}

int archivefs_rmdir(const char* path) {
  char fpath[PATH_MAX];
  fullpath(fpath, path);

  FileSystem* fs;
  FileNode* node;
  int ret = 0;

  if (!getFile(fpath, &fs, &node)) {
    ret = rmdir(fpath);
    if (ret) {
      ret = errno;
      print_err("RMDIR", path, ret);
    }
    return -ret;
  }

  /* Je požadováno smazání archivu */
  if (node->type == FileNode::ROOT_NODE) {
    FusePrivate* fuse_data = PRIVATE_DATA;
    fuse_data->filesystems->erase(fs->archive_name);
    delete fs;
    ret = unlink(fpath);
    if (ret) {
      ret = errno;
      print_err("RMDIR", path, ret);
    }
    return -ret;
  }

  ret = fs->remove(node);
  if (ret)
    print_err("RMDIR", path, ret);

  return -ret;
}

int archivefs_release(const char *path, struct fuse_file_info *info) {
  (void)path;

  FusePrivate* fuse_data = PRIVATE_DATA;
  FileHandle* fh;

  /* Zde je stejná logika jako u archivefs_read.
   */
  if (fuse_data->mode == FusePrivate::ARCHIVE_MOUNTED) {
    fh = reinterpret_cast<FileHandle*>(info->fh);
    fh->first->close(fh->second);
    delete fh;
  } else {
    char fpath[PATH_MAX];
    fullpath(fpath, path);

    if (access(fpath, F_OK) == 0) {
      close(info->fh);
    } else {
      fh = reinterpret_cast<FileHandle*>(info->fh);
      fh->first->close(fh->second);
      delete fh;
    }
  }
  return 0;
}

int archivefs_opendir(const char *path, struct fuse_file_info *info) {
  char fpath[PATH_MAX];
  fullpath(fpath, path);

  /* Stejná logika jako u archivefs_open.
   */

  DIR* dir;
  if ((dir = opendir(fpath)) != NULL) {
    info->fh = intptr_t(dir);
    return 0;
  }

  FileSystem* fs;
  FileNode* node;

  if (!getFile(fpath, &fs, &node)) {
    print_err("OPENDIR", path, ENOENT);
    return -ENOENT;
  }

  struct fuse_context* context = fuse_get_context();
  if (fs->access(node, R_OK, context->uid, context->gid))
    return -EACCES;

  info->fh = intptr_t(new FileHandle(fs, node));

  return 0;
}

int archivefs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                      off_t offset, struct fuse_file_info *info) {
  (void)offset;
  (void)path;

  FusePrivate* fuse_data = PRIVATE_DATA;
  FileHandle* fh = NULL;
  int ret;

  /* Stejná logika jako u archivefs_read.
   */
  if (fuse_data->mode == FusePrivate::ARCHIVE_MOUNTED) {
    fh = reinterpret_cast<FileHandle*>(info->fh);
  } else {
    char fpath[PATH_MAX];
    fullpath(fpath, path);
    struct stat dir_info;
    if (stat(fpath, &dir_info) == 0 && S_ISDIR(dir_info.st_mode)) {
      DIR* dir = reinterpret_cast<DIR*>(info->fh);
      struct dirent* file;

      // Every directory contains at least two entries: . and ..  If my
      // first call to the system readdir() returns NULL I've got an
      // error; near as I can tell, that's the only condition under
      // which I can get an error from readdir()
      file = readdir(dir);
      if (file == NULL) {
        ret = errno;
        print_err("READDIR", path, ret);
        return -ret;
      } else {
        do {
          if (filler(buf, file->d_name, NULL, 0) != 0) {
            return -ENOMEM;
          }

        } while ((file = readdir(dir)) != NULL);

        return 0;
      }
    } else {
      fh = reinterpret_cast<FileHandle*>(info->fh);
    }
  }

  filler(buf, ".", NULL, 0);
  filler(buf, "..", NULL, 0);

  FileList* files = fh->first->readDir(fh->second);
  for (FileList::const_iterator it = files->begin();
       it != files->end(); ++it) {
    if (filler(buf, (*it)->name_ptr, NULL, 0) != 0) {
      print_err("READDIR", path, ENOMEM);
      return -ENOMEM;
    }
  }

  return 0;
}

int archivefs_releasedir(const char *path, struct fuse_file_info *info) {
  (void)path;

  FusePrivate* fuse_data = PRIVATE_DATA;

  /* Stejná logika jako u archivefs_read.
   */

  if (fuse_data->mode == FusePrivate::ARCHIVE_MOUNTED) {
    delete reinterpret_cast<FileHandle*>(info->fh);
  } else {
    char fpath[PATH_MAX];
    fullpath(fpath, path);

    FileSystem* fs;
    if (getFile(fpath, &fs, NULL)) {
      delete reinterpret_cast<FileHandle*>(info->fh);
    } else {
      closedir(reinterpret_cast<DIR*>(info->fh));
    }
  }
  return 0;
}

int archivefs_access(const char* path, int mask) {
  char fpath[PATH_MAX];
  fullpath(fpath, path);
  FileSystem* fs;
  FileNode* node;
  int ret;
  struct fuse_context* context = fuse_get_context();

  if (!getFile(fpath, &fs, &node)) {
    ret = access(fpath, mask);
    if (ret) {
      ret = errno;
      print_err("ACCESS", path, ret);
    }
    return -ret;
  }

  ret = fs->access(node, mask, context->uid, context->gid);
  if (ret)
    print_err("ACCESS", path, ret);
  return -ret;
}

int archivefs_utimens(const char* path, const struct timespec times[2]) {
  char fpath[PATH_MAX];
  fullpath(fpath, path);

  FileSystem* fs;
  FileNode* node;
  int ret;

  if (!getFile(fpath, &fs, &node)) {
    ret = utimensat(0, fpath, times, 0);
    if (ret) {
      ret = errno;
      print_err("UTIMENS", path, ret);
    }
    return -ret;
  }

  ret = fs->utimens(node, times);
  if (ret)
    print_err("UTIMENS", path, ret);
  return -ret;
}

int archivefs_chmod(const char* path, mode_t mode) {
  char fpath[PATH_MAX];
  fullpath(fpath, path);

  FileSystem* fs;
  FileNode* node;

  int ret;

  if (!getFile(fpath, &fs, &node)) {
    if ((ret = chmod(fpath, mode)) != 0) {
      ret = errno;
      print_err("CHMOD", path, ret);
    }
    return -ret;
  }

  node->file_info.st_mode = mode;
  return 0;
}

