/* Project:  ArchiveFS
 * Author:   Michal SAMEK
 * Email:    xsamek01@fit.vutbr.cz
 * Desc:     Implementation of runtime drivers loading
 * Modified: 04/2012
 */

#include <cstring>
#include <climits>
#include <ctype.h>
#include <ctime>
#include <dirent.h>

#ifdef HAVE_LIBMAGIC
#include <magic.h>
#endif

#include "drivers.hpp"

DriversVector* drivers = new DriversVector;
char* path_to_drivers = NULL;

void strToLower(char* str) {
  ssize_t len = strlen(str);
  for (int i = 0; i < len; ++i) {
    str[i] = tolower(str[i]);
  }
}

/* Pokud chceme hledat poslední koncovku jako ext předej NULL.
 * Pokud chceš hledat koncovku od konkrétního místa, ext ukazuje PŘED toto místo.
 */
const char* findFileExt(const char* path, const char* ext) {
  if (ext == NULL) {
    ext = path;
    while (*ext != '\0') ++ext;
  } else
    if (ext < path) return NULL;  //chyba !

  while (ext != path && *ext != '.') --ext;

  /* Název neobsahuje tečku => neznámý typ souboru */
  if (ext == path) {
    return NULL;
  }

  return (ext+1);   //posun na první platný znak
}

bool LOAD_STANDARD_DRIVERS() {
  short failed = 0;
  short drivers_found = 0;
  char driver_path[PATH_MAX];

  #ifdef RPATH
  strcpy(driver_path, RPATH);
  #else
  strcpy(driver_path, "/usr/local/lib");
  #endif
  if (path_to_drivers)
    strcpy(driver_path, path_to_drivers);

  size_t path_len = strlen(driver_path);
  driver_path[path_len] = '/';
  driver_path[path_len+1] = '\0';
  char* filename_ptr = driver_path+path_len+1;

  string filename;
  struct dirent *de;
  DIR* dp = opendir(driver_path);
  if (dp == NULL) {
    cerr << "Could not open directory containing drivers" << endl;
    return false;
  }

  while ((de = readdir(dp)) != NULL) {
    filename = de->d_name;
    if (STARTS_WITH(filename, "afs_") && ENDS_WITH(filename, "driver.so")) {
      ++drivers_found;
      strcpy(filename_ptr, de->d_name);
      if (!LOAD_DRIVER(driver_path))
        ++failed;
    }
  }
  if (drivers_found == failed)
    cerr << "No drivers in standard location have been found" << endl;

  closedir(dp);
  return true;
}

bool LOAD_DRIVER(const char* pathname) {
  regdr_ptr regdr;
  DriverHandle* driver_handle;
  dlerror();
  void* handle = dlopen(pathname, RTLD_NOW);
  char* err = dlerror();

  if (handle == NULL) {
    cerr << "Driver load error: " << err << endl;
    return false;
  }
  regdr = (regdr_ptr)dlsym(handle, "REGISTER_DRIVER");
  if (regdr == NULL) {
    cerr << "Driver load error: Unrecognized driver" << endl;
    dlclose(handle);
    return false;
  }
  driver_handle = regdr();
  driver_handle->handle = handle;
  drivers->push_back(driver_handle);

  return true;
}

#ifdef HAVE_LIBMAGIC
ArchiveType* TYPE_BY_MIME(const char* mime) {
  for (unsigned i = 0; i < drivers->size(); ++i) {
    for (unsigned j = 0; j < drivers->at(i)->archive_types.size(); ++j) {
      if (strcmp(mime, drivers->at(i)->archive_types[j]->mime_text) == 0) {
        return drivers->at(i)->archive_types[j];
      }
    }
  }
  return NULL;
}
#endif

ArchiveType* TYPE_BY_EXT(const char* ext) {
  for (unsigned i = 0; i < drivers->size(); ++i) {
    for (unsigned j = 0; j < drivers->at(i)->archive_types.size(); ++j) {
      if (strcmp(ext, drivers->at(i)->archive_types[j]->extension) == 0) {
        return drivers->at(i)->archive_types[j];
      }
    }
  }
  return NULL;
}

ArchiveType* GET_TYPE(const char* path) {
  if (path == NULL) return NULL;

  ArchiveType* ret = NULL;

#ifdef HAVE_LIBMAGIC
  magic_t cookie = magic_open(MAGIC_RAW|MAGIC_MIME_TYPE);
  if (cookie == NULL) goto extension_resolution;

  magic_setflags(cookie, MAGIC_PRESERVE_ATIME);

  if (magic_load(cookie, FILE_MAGIC) != 0) {
    magic_close(cookie);
    goto extension_resolution;
  }

  const char* mime;
  if ((mime = magic_file(cookie, path)) == NULL) {
    magic_close(cookie);
    goto extension_resolution;
  }

  ret = TYPE_BY_MIME(mime);
  magic_close(cookie);

  if (ret) return ret;

  extension_resolution:
#endif

  char ext_low[10];
  const char* ext = findFileExt(path, NULL);
  if (ext == NULL) return NULL;
  strcpy(ext_low, ext);
  strToLower(ext_low);

  ret = TYPE_BY_EXT(ext_low);
  if (ret) return ret;

  ext = findFileExt(path, ext-2);  // musíme se dostat až před tečku
  if (ext == NULL) return NULL;
  strcpy(ext_low, ext);
  strToLower(ext_low);

  return TYPE_BY_EXT(ext_low);
}

void UNLOAD_DRIVERS() {
  free(path_to_drivers);
  while (!drivers->empty()) {
    delete drivers->back();
    drivers->pop_back();
  }
  delete drivers;
}

void PRINT_DRIVERS_SUPPORT() {
  cout << "Currently are supported this archives: " << endl;
  cout << "Extension\tSupport\t\t\tMime" << endl;
  for (uint i = 0; i < drivers->size(); ++i) {
    for (uint j = 0; j < drivers->at(i)->archive_types.size(); ++j) {
      cout << drivers->at(i)->archive_types[j]->extension << "\t"
            << ((drivers->at(i)->archive_types[j]->write_support)?"\tread/write support\t":
                                                                  "\tread support      \t")
            << drivers->at(i)->archive_types[j]->mime_text << "\t"
            << endl;
    }
  }
}

void generateNewArchiveName(string& name) {
  time_t t = time(NULL);
  char date_time[18];
  strftime(date_time, 18, "%Y-%m-%d %H:%M", localtime(&t));
  string ext = name.substr(name.find_last_of('.'));
  name.erase(name.find_last_of('.'));
  name.push_back('_');
  name.append("edit (");
  name.append(date_time);
  name.append(")");
  name.append(ext);
}
