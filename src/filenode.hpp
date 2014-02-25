/* Project:  ArchiveFS
 * Author:   Michal SAMEK
 * Email:    xsamek01@fit.vutbr.cz
 * Desc:     Header file for filenode.cpp
 * Modified: 04/2012
 */

#ifndef FILENODE_HPP
#define FILENODE_HPP

#include <map>
#include <vector>
#include <string>
#include <iostream>
#include <cstdlib>
#include <sys/stat.h>
#include <pthread.h>

#include "buffer.hpp"

using namespace std;

/* Forward deklarace pro FileNode */
class FileNode;

/* Pole s ukazateli na synovske soubory - obsah adresaru */
typedef vector<FileNode*> FileList;

struct ltstr {
  bool operator() (const char* s1, const char* s2) const {
    return strcmp(s1, s2) < 0;
  }
};

/** \typedef
  *  Asociativní pole mapující řetězec obsahující cestu k souboru na ukazatel
  *  vztahující se k objektu obsahujícímu informace o tomto souboru.
  */
typedef map<const char*, FileNode*, ltstr> FileMap;

/** \class FileData
 * Trida uchovavajici informace o "pozici" uzitecnych dat souboru
 * uvnitr archivu.
 * MUSI BYT POLYMORFNI - ovladace archivu pak ukazatele na tuto bazovou
 * tridu pretypovavaji pomoci dynamic_cast.
 */
class FileData {
public:
  virtual ~FileData() {};
};

/** \class FileNode
 * Objekty této třídy implementují jednotlivé soubory/adresáře virtualního
 * souborového systému.
 *
 * Pro definování pozice užitečných dat souboru v rámci archivu slouží třída
 * FileData, ze které vždy konkrétní ovladač podědí svou vlastní třídu,
 * pomocí které lze určovat pozici dat v archivech daného typu.
 * Objekty typu FileNode pak obsahují ukazatel na bázovou třídu FileData,
 * kterou vždy ovladač přetypuje pomocí dynamic_cast.
 *
 * Obsah souboru může být bufferován v paměti, zpravidla se tak děje u
 * archivů s kompresí popř. u souborů, které byly vytvořeny, ale ještě
 * nebyly zapsány do archivu.
 * Zdali je soubor bufferován poznáme podle adresy uložené v atributu buffer.
 */
class FileNode {
  public:
    /// Rozlišuje typ souboru
    enum NodeType {ROOT_NODE, FILE_NODE, DIR_NODE} type;

    /**
     * Konstuktoru se předává absolutní cesta v rámci virtuálního filesystému
     * NEZAČÍNAJÍCÍ a NEKONČÍCÍ slashem!
     * Přes parametr data se dá předat objekt odvozený ze třídy FileData
     * definující pozici dat souboru uvnitř archivu, příp NULL např pro soubory
     * jejichž obsah se nechází v paměti.
     * Parametr _type definuje typ vytvářeného uzlu/souboru.
     *
     * Řetězce obsahující celou cestu k souboru, cestu k nadřazenému adresáři
     * a jméno souboru jsou dynamicky alokovány a uvolněny v destruktoru.
     *
     * Kořenový adresář má path, name i pathname nastaveny na NULL. Stejně
     * tak soubory jenž se nacházejí v kořenovém adresáři archivu mají NULL
     * jako cestu k nadřazenému adresáři (atribut path).
     *
     * Pro adresáře jsou nastavena výchozí práva rwxr-xr-x. Pro regulární
     * soubory rw-r--r--.
     */
    FileNode(const char* _pathname, FileData* _data, enum NodeType _type);

    /**
     * Destruktor uvolní řetězce path, name, pathname - pokud se nejedná
     * o kořenový uzel.
     * Stejně tak pokud je vytvořen buffer, případně objekt data tak jsou
     * tito uvolněni.
     */
    ~FileNode();

    static uid_t uid;
    static gid_t gid;

    /**
     * Přidá potomka odkazovaného přes node.
     * Děje se tak v případě adresářů pro přidávání obsažených souborů.
     * Sám inkrementuje čítač odkazu ve struktuře stat.
     */
    void addChild(FileNode* node);

    /**
     * Odebere potomka odkazovaného přes node.
     * Provede pouze odebrání ze seznamu potomku - ovolnění odebíraného uzlu
     * zde není zajištěno.
     * Sám dekrementuje čítač odkazu ve struktuře stat.
     */
    void removeChild(FileNode* node);

    void listChildren() {
      cout << "Children of " << (pathname?pathname:"ROOT_DIR")
           << " (" << children.size() << ")" << endl;
      for (FileList::iterator it = children.begin(); it != children.end(); ++it) {
        cout << "\t" << (*it)->pathname << endl;
      }
      cout << endl;
    }

    /**
     * Nastavuje velikost uzlu na size.
     * Uloženo v parametru struktury stat (atribut file_info).
     */
    void setSize(offset_t size);

    /// Vrátí velikost uzlu v bytech
    offset_t getSize();

    /* Nasledujici atributy jsou nastaveny pri konstrukci objektu            */
    /*************************************************************************/
    /* Korenovy uzel ma pathname, path i name NULL */

    /// Řetězec s celou cestou k objektu.
    char*         pathname;

    /// Ukazatel na začátek jména souboru
    char*         name_ptr;

    /// Řetězec obsahující původní jméno souboru v archivu
    char*         original_pathname;

    /**
     * Ukazatel na buffer obsahující data souboru. V případě, že je tento
     * atribut roven NULL, data souboru nejsou bufferována.
     */
    Buffer*       buffer;

    /// Čítač referencí - počet otevření souboru.
    unsigned      ref_cnt;

    /// Příznak, zdali došlo ke změně dat souboru.
    bool          changed;


    /* Nasledujici atributy musi nastavit objekt FileSystem                  */
    /*************************************************************************/
    /// Ukazatel na nadřazený uzel/adresář.
    FileNode*     parent;

    /// Vektor s ukazateli na potomky - obsažené soubory.
    FileList     children;


    /* Nasledujici atributy musi nastavit ovladac archivu                    */
    /*************************************************************************/
    /// Ukazatel na bázovou třídu objektu určujícího pozici dat v archivu
    FileData*     data;

    /**
     * Systémová struktura stat obsahující informace o souboru.
     * Ovladač archivu musí doplnit velikost souboru a timestampy.
     * Ostatní prvky struktury jsou inicializovány při konstrukci objektu
     * následovně (platí pro všechny soubory):
     *   st_uid     = euid procesu
     *   st_gid     = egid procesu
     *   st_blksize = STANDART_BLOCK_SIZE
     *   st_size    = soubor 0, adresář STANDART_BLOCK_SIZE
     *   st_blocks  = soubor 0, adresář 1
     *   st_nlink   = soubor 1, adresář 2
     */
    struct stat   file_info;

    pthread_rwlock_t lock;
};

ostream& operator<< (ostream&, FileNode&);

#endif // FILENODE_H
