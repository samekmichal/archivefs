/* Project:  ArchiveFS
 * Author:   Michal SAMEK
 * Email:    xsamek01@fit.vutbr.cz
 * Desc:     Header file for source file implementing FUSE filesystem
 * Modified: 04/2012
 */

#define ARCHIVE_FS_VERSION ("1.0 (march 2012)")

#include <map>
#include <string>
#include <cerrno>
#include <cstddef>

#define FUSE_USE_VERSION 28
#include <fuse.h>

#include "filesystem.hpp"

#include <boost/algorithm/string/predicate.hpp>
#define ENDS_WITH(STRING, ENDING) \
        (boost::algorithm::ends_with(STRING, ENDING))

using namespace std;

void printHelp();

typedef map<const char*, FileSystem*, ltstr> FSMap;
typedef pair<FileSystem*, FileNode*> FileHandle;
extern char* path_to_drivers;
extern DriversVector* drivers;

#define PRIVATE_DATA \
        (reinterpret_cast<FusePrivate*>(fuse_get_context()->private_data))

/**
 * Wrapper class pro asociativní pole s řetězcovým klíčem a s hodnotou typu
 * ukazatel na objekt FileSystem.
 * Definuje pouze potřebné metody.
 * THREAD SAFE & DESTROYING CONTAINED OBJECTS!
 */
class FileSystemS {
  /// Asociativní pole s obsaženými soubory.
  FSMap map;

  /// Mutex použitý k synchronizaci.
  pthread_mutex_t mutex;
public:
  FileSystemS() {
    pthread_mutex_init(&mutex, NULL);
  }

  ~FileSystemS() {
    FSMap::iterator it;
    for (it = map.begin(); it != map.end(); ++it) {
      delete it->second;
    }
    pthread_mutex_destroy(&mutex);
  }

  inline void insert(FileSystem* fs) {
    pthread_mutex_lock(&mutex);
    map[fs->archive_name] = fs;
    pthread_mutex_unlock(&mutex);
  }

  inline void erase(const char* key) {
    pthread_mutex_lock(&mutex);
    FSMap::iterator it = map.find(key);
    map.erase(it);
    pthread_mutex_unlock(&mutex);
  }

  FileSystem* find (const char* key) {
    pthread_mutex_lock(&mutex);
    FSMap::iterator it = map.find(key);
    if (it == map.end()) {
      pthread_mutex_unlock(&mutex);
      return NULL;
    } else {
      pthread_mutex_unlock(&mutex);
      return it->second;
    }
  }
};


/** \class FusePrivate
 * Třída uchovávající privátní data FUSE filesystému.
 * Pro získání objektu této třídy lze použít makro PRIVATE_DATA.
 */
class FusePrivate {
public:
  FusePrivate(int argc, char* argv[]) {
    args.argc      = argc;
    args.argv      = argv;
    args.allocated = 0;
    filesystems    = new FileSystemS;
    keep_trash     = false;
    create_archive = false;
    read_only      = false;
    load_driver    = false;
    respect_rights = false;
    keep_original  = false;
    buffer_limit   = 100;
    drivers_path   = NULL;
    mounted = mountpoint = NULL;
  }

  ~FusePrivate() {
    fuse_opt_free_args(&args);

    delete filesystems;

    UNLOAD_DRIVERS();

    free(mounted);
    free(mountpoint);
    free(drivers_path);
  }

  struct fuse_args args;

  /**
   * Mod programu: buď je namountován jeden archiv,
   * nebo je namountován adresář, jenž archivy obsahuje.
   */
  enum MODE {ARCHIVE_MOUNTED, FOLDER_MOUNTED} mode;

  /**
   * Asociativní pole s objekty typu FileSystem. Každý fyzický archiv na disku
   * je asociován ke konkrétnímu objektu FileSystem, jenž uchovává metadata o
   * souborech uvnitř archivu a také implementuje funkce pro manipulaci
   * s těmito soubory.
   * Klíčem v tomto poli je cesta k fyzickému archivu - cesta musí být
   * absolutní vůči kořeni fyzického souborového systému.
   */
  FileSystemS* filesystems;

  /**
   * Řetězec obsahující cestu k mountpointu.
   */
  char* mountpoint;

  /**
   * Řetězec obsahující cestu ke zdrojovému souboru virtuálního filesystému.
   * Buď archív nebo adresář. Cesta opět  musí být absolutní vůči kořeni
   * fyzického souborového systému.
   */
  char* mounted;

  bool keep_trash;
  bool create_archive;
  bool read_only;
  bool load_driver;
  bool respect_rights;
  bool keep_original;
  int buffer_limit;
  char* drivers_path;
};


/* Konfigurace pro zpracování parametrů příkazové řádky pomocí FUSE */
#define AFS_OPT(t, p, v) { t, offsetof(FusePrivate, p), v }
enum {KEY_HELP, KEY_VERSION, KEY_VERBOSE, KEY_SUPPORTED};
enum {FUSE_ERROR = -1, FUSE_DISCARD = 0, FUSE_KEEP = 1};

static struct fuse_opt fuse_opts[] = {
  AFS_OPT("-t",                      keep_trash,     true),
  AFS_OPT("--keep-trash",            keep_trash,     true),
  AFS_OPT("-c",                      create_archive, true),
  AFS_OPT("--create",                create_archive, true),
  AFS_OPT("-r",                      read_only,      true),
  AFS_OPT("--read-only",             read_only,      true),
  AFS_OPT("-R",                      respect_rights, true),
  AFS_OPT("--respect-rights",        respect_rights, true),
  AFS_OPT("--drivers-path=%s",       drivers_path,   0),
  AFS_OPT("--load-drivers",          load_driver,    true),
  AFS_OPT("--buffer-limit=%i",       buffer_limit,   0),
  AFS_OPT("--keep-original",         keep_original,  true),


  FUSE_OPT_KEY("-l",                 KEY_SUPPORTED),
  FUSE_OPT_KEY("--list-supported",   KEY_SUPPORTED),
  FUSE_OPT_KEY("-V",                 KEY_VERSION),
  FUSE_OPT_KEY("--version",          KEY_VERSION),
  FUSE_OPT_KEY("-v",                 KEY_VERBOSE),
  FUSE_OPT_KEY("--verbose",          KEY_VERBOSE),
  FUSE_OPT_KEY("-h",                 KEY_HELP),
  FUSE_OPT_KEY("--help",             KEY_HELP),
  FUSE_OPT_END
};

const char* HELP_TEXT = "\nArchiveFS\n"
"-----------------------------------------------------\n"
"Author: Michal SAMEK, xsamek01 <at> stud.fit.vutbr.cz\n"
"FIT VUTBR 2012\n\n"
"afs is a program for creating virtual filesystem\n"
"(using FUSE) from archive files\n\n"
"as a source file you can use single archive file or\n"
"folder containing archive files (or you can create one)\n\n"
"usage: afs <source_file> <mountpoint> [OPTIONS]\n\n"
"ArchiveFS options:\n"
"    -h  --help\t\t\tprint full help\n"
"    -v  --verbose\t\tbe verbose\n"
"    -V  --version\t\tprint version of afs\n"
"    -t  --keep-trash\t\tkeep trash in the source file\n"
"    -l  --list-supported\tlist supported file archives\n"
"    -r  --read-only\t\tcreate read-only filesystem\n"
"        --keep-original\t\tkeep original archive file\n"
"    -R  --respect-rights\trespect file access rights stored in archive\n"
"    -c  --create\t\twill create new archive file\n"
"        --load-drivers %s %s...\tload this drivers (space separated list) [specify last]\n"
"        --drivers-path=%s\tpath to other drivers [specify first]\n"
#ifdef RPATH
"\t\t\t\tstandard drivers were installed in "RPATH"\n"
#endif
"        --buffer-limit=%i\tmax size (in MB) of memory buffer for keeping\n"
"\t\t\t\tdata of a single file\n"
"\t\t\t\tdefault (100), unlimited(-1), dont keep in memory(0)\n"
;

const char* RUN_AS_ROOT_WARN = "WARNING\n"
"Running afs as root opens security holes !!!\n"
"Entire FUSE filesystem will run with root privileges,\n"
"thus EVERY operation will be run with root privileges";


/*****************************************************************************/
/**************************** Prototypy funkcí *******************************/
/*****************************************************************************/

/**
 * Callback funkce pro zpracování parametrů příkazové řádky,
 * @see FUSE: zpracování parametrů
 */
int process_arg(void*, const char*, int, struct fuse_args);


/**
 * Pokud řetězec končí "známou" příponou vrací true, jinak false.
 * Známé přípony jsou uvedeny v drivers.hpp.
 * @see KNOWN_EXTENSIONS
 */
bool hasKnownExtension(string);

/**
 * Nastavuje programový mod, podle toho zdali si uživatel přeje
 * připojit souborový archiv nebo adresář obsahující archivy.
 * Mód je zaznamenán do objektu typu FusePrivate předaného odkazem.
 * @see FusePrivate::enum MODE
 */
int setMountMode(FusePrivate*);

/**
 * Provede konkatenaci řetězce předaného parametrem s cestou k připojenému
 * adresáři/souboru. Místo pro konkatenovaný řetězec již musí být alokováno.
 */
void fullpath(char*, const char*);

/**
 * Rozdělí cestu předanou parametrem na dvě části ...
 */
void parsePathName(char*, string&, string&);


bool getFile(char*, FileSystem**, FileNode**);

/**
 * Funkce zajišťující inicializaci celého filesystému.
 */
bool initialize(FusePrivate*);

/******************************************************************************
 * FUSE OPARATIONS
 *****************************************************************************/

/** Initialize filesystem
 *  The return value will passed in the private_data field of
 *  fuse_context to all file operations and as a parameter to the
 *  destroy() method.
 */
void* archivefs_init(struct fuse_conn_info* conn);

/** Clean up filesystem
 *  Called on filesystem exit.
 */
void archivefs_destroy(void* private_data);

/** Get file attributes.
 *  Similar to stat().  The 'st_dev' and 'st_blksize' fields are
 *  ignored.  The 'st_ino' field is ignored except if the 'use_ino'
 *  mount option is given.
 */
int archivefs_getattr(const char* path, struct stat* info);

/** Get file system statistics
  *
  * The 'f_frsize', 'f_favail', 'f_fsid' and 'f_flag' fields are ignored
  *
  * Replaced 'struct statfs' parameter with 'struct statvfs' in
  * version 2.5
  */
int archivefs_statfs(const char *path, struct statvfs *info);

/** File open operation
 *  No creation (O_CREAT, O_EXCL) and by default also no
 *  truncation (O_TRUNC) flags will be passed to open(). If an
 *  application specifies O_TRUNC, fuse first calls truncate()
 *  and then open(). Only if 'atomic_o_trunc' has been
 *  specified and kernel version is 2.6.24 or later, O_TRUNC is
 *  passed on to open.
 *  Unless the 'default_permissions' mount option is given,
 *  open should check if the operation is permitted for the
 *  given flags. Optionally open may also return an arbitrary
 *  filehandle in the fuse_file_info structure, which will be
 *  passed to all file operations.
 */
int archivefs_open(const char* path, struct fuse_file_info* info);

/** Read data from an open file
 *  Read should return exactly the number of bytes requested except
 *  on EOF or error, otherwise the rest of the data will be
 *  substituted with zeroes.  An exception to this is when the
 *  'direct_io' mount option is specified, in which case the return
 *  value of the read system call will reflect the return value of
 *  this operation.
 */
int archivefs_read(const char *path, char *buffer, size_t bufsize,
                   off_t offset, struct fuse_file_info *info);

/** Read the target of a symbolic link
 *
 * The buffer should be filled with a null terminated string.  The
 * buffer size argument includes the space for the terminating
 * null character.  If the linkname is too long to fit in the
 * buffer, it should be truncated.  The return value should be 0
 * for success.
 */
// int (*readlink) (const char *, char *, size_t);

/** Release an open file
 *  Release is called when there are no more references to an open
 *  file: all file descriptors are closed and all memory mappings
 *  are unmapped.
 *  For every open() call there will be exactly one release() call
 *  with the same flags and file descriptor.  It is possible to
 *  have a file opened more than once, in which case only the last
 *  release will mean, that no more reads/writes will happen on the
 *  file.  The return value of release is ignored.
 */
int archivefs_release(const char *path, struct fuse_file_info *info);

/** Open directory
 *  Unless the 'default_permissions' mount option is given,
 *  this method should check if opendir is permitted for this
 *  directory. Optionally opendir may also return an arbitrary
 *  filehandle in the fuse_file_info structure, which will be
 *  passed to readdir, closedir and fsyncdir.
 */
int archivefs_opendir(const char *path, struct fuse_file_info *info);

/** Read directory
 *  The filesystem may choose between two modes of operation:
 *
 *  1) The readdir implementation ignores the offset parameter, and
 *  passes zero to the filler function's offset.  The filler
 *  function will not return '1' (unless an error happens), so the
 *  whole directory is read in a single readdir operation.  This
 *  works just like the old getdir() method.
 *
 *  2) The readdir implementation keeps track of the offsets of the
 *  directory entries.  It uses the offset parameter and always
 *  passes non-zero offset to the filler function.  When the buffer
 *  is full (or an error happens) the filler function will return
 *  '1'.
 */
int archivefs_readdir(const char *path, void *, fuse_fill_dir_t filler, off_t offset,
    struct fuse_file_info *info);

/** Release directory
 */
int archivefs_releasedir(const char *path, struct fuse_file_info *info);

/** Change the permission bits of a file
 */
int archivefs_chmod(const char* path, mode_t mode);

/** Change the owner and group of a file */
// int archivefs_chown(const char *, uid_t, gid_t);


/** Create a file node
  *
  * This is called for creation of all non-directory, non-symlink
  * nodes.  If the filesystem defines a create() method, then for
  * regular files that will be called instead.
  */
int archivefs_mknod(const char *, mode_t, dev_t);

/** Create and open a file
 *
 * If the file does not exist, first create it with the specified
 * mode, and then open it.
 *
 * If this method is not implemented or under Linux kernel
 * versions earlier than 2.6.15, the mknod() and open() methods
 * will be called instead.
 *
 * Introduced in version 2.5
 */
int archivefs_create(const char *, mode_t, struct fuse_file_info *);

/** Create a directory
 *
 * Note that the mode argument may not have the type specification
 * bits set, i.e. S_ISDIR(mode) can be false.  To obtain the
 * correct directory type bits use  mode|S_IFDIR
 */
int archivefs_mkdir(const char *, mode_t);

/** Write data to an open file
 *
 * Write should return exactly the number of bytes requested
 * except on error.  An exception to this is when the 'direct_io'
 * mount option is specified (see read operation).
 *
 * Changed in version 2.2
 */
int archivefs_write(const char *, const char *, size_t, off_t,
        struct fuse_file_info *);

/** Remove a file */
int archivefs_unlink(const char *);

/** Remove a directory */
int archivefs_rmdir(const char *);

/** Rename a file */
int archivefs_rename(const char *, const char *);

/** Create a hard link to a file */
// int archivefs_link(const char *, const char *);

/** Change the size of a file */
int archivefs_truncate(const char *, off_t);

/**
 * Change the size of an open file
 *
 * This method is called instead of the truncate() method if the
 * truncation was invoked from an ftruncate() system call.
 *
 * If this method is not implemented or under Linux kernel
 * versions earlier than 2.6.15, the truncate() method will be
 * called instead.
 *
 * Introduced in version 2.5
 */
// int archivefs_ftruncate(const char *, off_t, struct fuse_file_info *);

/** Possibly flush cached data
 *
 * BIG NOTE: This is not equivalent to fsync().  It's not a
 * request to sync dirty data.
 *
 * Flush is called on each close() of a file descriptor.  So if a
 * filesystem wants to return write errors in close() and the file
 * has cached dirty data, this is a good place to write back data
 * and return any errors.  Since many applications ignore close()
 * errors this is not always useful.
 *
 * NOTE: The flush() method may be called more than once for each
 * open().  This happens if more than one file descriptor refers
 * to an opened file due to dup(), dup2() or fork() calls.  It is
 * not possible to determine if a flush is final, so each flush
 * should be treated equally.  Multiple write-flush sequences are
 * relatively rare, so this shouldn't be a problem.
 *
 * Filesystems shouldn't assume that flush will always be called
 * after some writes, or that if will be called at all.
 *
 * Changed in version 2.2
 */
// int archivefs_flush(const char *, struct fuse_file_info *);

/**
 * Check file access permissions
 *
 * This will be called for the access() system call.  If the
 * 'default_permissions' mount option is given, this method is not
 * called.
 *
 * This method is not called under Linux kernel versions 2.4.x
 *
 * Introduced in version 2.5
 */
int archivefs_access(const char *, int);

/**
 * Change the access and modification times of a file with
 * nanosecond resolution
 *
 * Introduced in version 2.6
 */
int archivefs_utimens(const char *, const struct timespec times[2]);
