/* Project:  ArchiveFS
 * Author:   Michal SAMEK
 * Email:    xsamek01@fit.vutbr.cz
 * Desc:     This header file contains declaration of abstract class
 *           ArchiveDriver, which define obligatory interface for
 *           other archive drivers.
 * Modified: 04/2012
 */

#ifndef ARCHIVEDRIVER_HPP
#define ARCHIVEDRIVER_HPP

#include <vector>
#include <cerrno>
#include <cstring>

#include <inttypes.h>
#include <dlfcn.h>

#include "filenode.hpp"

#define STANDART_BLOCK_SIZE 4096

using namespace std;

class FileSystem;
class ArchiveDriver;

/** \class AbstractFactory
 * Abstraktní továrna na ovladače
 */
class AbstractFactory {
public:
  virtual ArchiveDriver* getDriver(const char*, bool) = 0;
};

/** \struct ArchiveType
 * Objekty této třídy definují typy archivů, které daný ovladač podporuje.
 */
struct ArchiveType {
  /// Přípona pro konkrétní typ souboru
  const char* extension;

  /// MIME typ pro konkrétní typ souboru
  const char* mime_text;

  /// Ukazatel na konkrétní továrnu vyrábějící objekty ovladače pro daný typ souboru
  AbstractFactory* factory;

  /// Flag definující, zdali je pro tento typ dostupná podpora zápisu
  bool write_support;

  ArchiveType(const char* _ext, const char* _mime, AbstractFactory* _factory, bool w_sup = false)
  : factory(_factory), write_support(w_sup) {
    extension = strdup(_ext);
    mime_text = strdup(_mime);
  }

  ~ArchiveType() {
    free((void*)extension);
    free((void*)mime_text);
    delete factory;
  }
};


/** \class DriverHandle
 * Handle ke knihovně s ovladačem.
 * Bývá návratovou hodnotou registračních funkcí knihoven s ovladači.
 */
struct DriverHandle {
  /// Seznam ovladačem podporovaných typů archivů.
  vector<ArchiveType*> archive_types;

  /// Handle k načtené knihovně. Nutný k jejímu uvolnění pomocí dlclose().
  void* handle;

  DriverHandle() : handle(NULL) {}

  ~DriverHandle() {
    dlclose(handle);
    handle = NULL;
    while (!archive_types.empty()) {
      delete archive_types.back();
      archive_types.pop_back();
    }
  }
};


/** \class ArchiveDriver
 * Abstraktní třída definující rozhraní ovladačů pro další typy archivů
 * Ovladač archivu musí v každém FileNodu vyplnit tyto pole struktury stat:
 * st_uid, st_gid, st_size, st_blksiez, st_blocks, st_*time
 */
class ArchiveDriver {
  public:
    /**
     * Konstruktor si uloží cestu k přidruženému archivu a získá efektivní UID
     * a GID uživatele, pod kterým proces běží. Tyto práva jsou následně použita
     * při definici práv souborů obsažených v archivu.
     */
    ArchiveDriver (const char* archive) {
      archive_path = strdup(archive);
    }

    /**
     * Destruktor je virtuální, každý archiv si dynamicky alokované místo
     * spravuje sám.
     */
    virtual ~ArchiveDriver() {free(archive_path);};

    /**
     * Vytvoří ~ inicializuje objekt typu FileSystem, jenž ukládá/spravuje
     * informace o souborech obsažených v archivu.
     */
    virtual bool buildFileSystem(FileSystem*) = 0;

    /**
     * Otevře uzel předaný parametrem ke čtení.
     */
    virtual bool open(FileNode*) = 0;

    /**
     * Čte data z uzlu předaného parametrem do připraveného bufferu o dané
     * velikosti.
     */
    virtual int read(FileNode*, char*, size_t, offset_t) = 0;

    /**
     * Uzavře soubor.
     */
    virtual void close(FileNode*) = 0;

    /**
     * Uloží obsah souborového archivu. Předává se asociativní pole FileMap
     * obsahující jako hodnoty ukazatele na objekty FileNode. A vektor FileList
     * smazaných souborů.
     */
    virtual bool saveArchive(FileMap*, FileList*) = 0;

    static bool respect_rights;
    static bool keep_original;

    /** \class ArchiveError
     *  Objekty této třídy jsou použity pro vyjímky, kdy dojde k chybě při
     *  inicializaci ovladače - většinou otevření souboru.
     */
    class ArchiveError {};

  protected:
    ///Ukazatel na řetězec s cestou k souborovému archivu.
    char* archive_path;

};

#endif // ARCHIVEDRIVER_H
