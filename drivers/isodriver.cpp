/* Project:  ArchiveFS
 * Author:   Michal SAMEK
 * Email:    xsamek01@fit.vutbr.cz
 * Desc:     Driver for manipulating iso archives
 * Modified: 04/2012
 */

#include <cstring>
#include <cstdlib>
#include <climits>
#include <iostream>
#include <fstream>
#include <sstream>

#include "isodriver.hpp"
#include "filesystem.hpp"
#include "membuffer.hpp"
#include "filebuffer.hpp"

class IsoDriverFactory: public AbstractFactory {
public:
  ArchiveDriver* getDriver(const char* path, bool create) {
    return new IsoDriver(path, create);
  }
};

extern "C" {
  DriverHandle* REGISTER_DRIVER () {
    DriverHandle* h = new DriverHandle;
    h->archive_types.push_back(new ArchiveType("iso", "application/x-iso9660-image",
                                               new IsoDriverFactory, true));
    return h;
  }
}

void __attribute__ ((constructor)) my_init(void) {
  iso_init();
//   iso_set_msgs_severities((char*)"ALL", (char*)"ALL", (char*)"ISO ");
}

void __attribute__ ((destructor)) my_fini(void) {
  iso_finish();
}

ino_t IsoDriver::BufferStream::serial_id = 1;
IsoStreamIface IsoDriver::buffer_stream_iface = {
    4, /* version */
    "usr", //TODO: mělo by být user
    bufferStreamOpen,
    bufferStreamClose,
    bufferStreamGetSize,
    bufferStreamRead,
    bufferStreamIsRepeatable,
    bufferStreamGetId,
    bufferStreamFree,
    bufferStreamUpdateSize,
    bufferStreamGetInputStream,
    bufferStreamCmpIno,
    bufferStreamCloneStream
};


IsoDriver::IsoDriver(const char* _archive, bool create_archive)
  : ArchiveDriver(_archive),
    iso_source(NULL),
    iso_filesystem(NULL) {

  pthread_mutex_init(&mutex, NULL);

  /* Pokud vytváříme nový archiv, nebudeme dále pokračovat */
  if (create_archive) return;

  IsoReadOpts* ropts;

  /* Nastavíme zdroj dat - otevře soubor a nastaví manipulující funkce */
  if (iso_data_source_new_from_file(_archive, &iso_source) < 0) {
    throw ArchiveError();
  }

  if (iso_read_opts_new(&ropts, 0) < 0) {
    iso_data_source_unref(iso_source);
    throw ArchiveError();
  }

  /* Inicializuje struktury pro práci se souborem.
   * Zejména zjišťuje obsažená rozšíření.
   */
  if (iso_image_filesystem_new(iso_source, ropts, 1, &iso_filesystem) < 0) {
    iso_data_source_unref(iso_source);
    iso_read_opts_free(ropts);
    throw ArchiveError();
  }

  iso_read_opts_free(ropts);

  /* iso_image_filesystem_new nechalo data source "otevřený"   */
  iso_data_source_unref(iso_source);

  return;
}

IsoDriver::~IsoDriver() {
  pthread_mutex_destroy(&mutex);

  if (iso_filesystem) {
    iso_filesystem->close(iso_filesystem);
    iso_filesystem_unref(iso_filesystem);
  }
}

bool IsoDriver::open(FileNode* node) {
  IsoFileData* casted_data = static_cast<IsoFileData*>(node->data);

  int ret;
  if ((ret = iso_file_source_open(casted_data->data)) < 0) {
    if (unsigned(ret) != ISO_FILE_ALREADY_OPENED) return false;
  }
  return true;
}

int IsoDriver::read(FileNode* node, char* buffer, size_t bytes, offset_t offset) {
  IsoFileData* casted_data = static_cast<IsoFileData*>(node->data);

  pthread_mutex_lock(&mutex);
  iso_file_source_lseek(casted_data->data, offset, SEEK_SET);
  int read = iso_file_source_read(casted_data->data, buffer, bytes);
  pthread_mutex_unlock(&mutex);
  return read;
}

void IsoDriver::close(FileNode* node) {
  IsoFileData* casted_data = static_cast<IsoFileData*>(node->data);

  iso_file_source_close(casted_data->data);
}

/* Funkce zpracovávající adresář v iso obrazu */
bool IsoDriver::buildDir(FileSystem* fs, IsoFileSource* dir) {
  IsoFileSource* file;
  FileNode* node;
  struct stat info;
  char* filename;
  bool new_node;
  bool success = true;

  int ret;
  if ((ret = iso_file_source_open(dir)) < 0) {
    if (unsigned(ret) != ISO_FILE_ALREADY_OPENED) return false;
  }

  /* iso_file_source_readdir zvyšuje počet referenci na file i na dir !!! */
  while (iso_file_source_readdir(dir, &file) == 1) {
    filename = iso_file_source_get_path(file); // file má teď 1 referenci

    node = fs->find(filename+1); //bez prvního lomítka
    if (node) {
      new_node = false;
      if (node->data == NULL)
        node->data = new IsoFileData(file);
      else
        static_cast<IsoFileData*>(node->data)->data = file;
    } else {
      new_node = true;
      iso_file_source_stat(file, &info);

      if (S_ISDIR(info.st_mode)) {
        node = new FileNode(filename+1, new IsoFileData(file), FileNode::DIR_NODE);
        if (!buildDir(fs, file)) success = false;
      } else
        node = new FileNode(filename+1, new IsoFileData(file), FileNode::FILE_NODE);

      node->setSize(info.st_size);
      node->file_info.st_atime = info.st_atime;
      node->file_info.st_ctime = info.st_ctime;
      node->file_info.st_mtime = info.st_mtime;

      /* Pokud není respektování práv v archivu definováno, jsou použity výchozí
       * oprávnění přidělena v konstruktoru FileNode
       */
      if (respect_rights) {
        node->file_info.st_mode  = info.st_mode;
        node->file_info.st_uid   = info.st_uid;
        node->file_info.st_gid   = info.st_gid;
      }
    }

    free(filename);

    if (new_node) {
      try {
        fs->append(node);
      }
      catch (FileSystem::AlreadyExists& existing) {
        existing.node->file_info = node->file_info;
        if (existing.node->data == NULL) existing.node->data = new IsoFileData;
        memcpy(existing.node->data, node->data, sizeof(IsoFileData));
        iso_file_source_ref(file);
        delete node;
        continue;
      }
    }
  }

  iso_file_source_close(dir);

  return success;
}

bool IsoDriver::buildFileSystem(FileSystem* fs) {
  IsoFileSource* root;
  FileNode* root_node;
  bool ret;

  root_node = fs->getRoot();

  /* DŮLEŽITÉ
   * nesnižovat reference u souborů
   */

  /* get_root zvyšuje počet referencí */
  if (iso_filesystem->get_root(iso_filesystem, &root) < 0) return false;
  root_node->data = new IsoFileData(root);

  ret = buildDir(fs, root);
  return ret;
}

bool IsoDriver::saveArchive (FileMap* files, FileList* deleted) {
  IsoImage* image;
  struct burn_source *burn_src;
  IsoReadOpts* ropts;
  IsoWriteOpts* wopts;
  const int buf_size = 2048;
  char buf[buf_size];
  int ret;


  if (iso_image_new("ArchiveFS", &image) < 0) {
    cerr << "IsoDriver: cannot create new image" << endl;
    return false;
  }

  iso_tree_set_follow_symlinks(image, 0);
  iso_tree_set_ignore_hidden(image, 0);
  /* Změněné soubory vždy nahraní existující */
  iso_tree_set_replace_mode(image, ISO_REPLACE_ALWAYS);


  /* Iso_source je nastaven pouze pokud se filesystem vytvářel z existujícího archivu.
   * Pokud se fs vztahuje k nově vytvořenému archivu je nastaven na NULL
   */
  if (iso_source) {
    /*
     * třeba zavřít všechny soubory
     * NENULOVAT cestu k datům !!! je dale použito ke zjištění zdali se jedná o nově
     * vytvořený soubor nebo o soubor modifikovaný
     */
    for (FileMap::iterator it = files->begin(); it != files->end(); ++it)
      delete it->second->data;

    iso_filesystem->close(iso_filesystem);
    iso_filesystem_unref(iso_filesystem);
    iso_filesystem = NULL;
    /* Extra reference navíc */
//       iso_data_source_ref(iso_source);

    iso_read_opts_new(&ropts, 0);
    if ((ret = iso_image_import(image, iso_source, ropts, NULL)) < 0) {
      cerr << "IsoDriver image_import failed: " << iso_error_to_msg(ret) << endl;
      iso_image_unref(image);
      iso_read_opts_free(ropts);
      return false;
    }
    iso_read_opts_free(ropts);
  }

  /* Úprava image v paměti *****************************************************/

  FileNode*     node;
  IsoNode*      iso_node;
  IsoDir*       iso_parent_node;
  BufferStream* stream;
//   vector<FileMap::iterator> nodes_to_free;

  /* Inicializace - každá cesta začína v iso obrazu slashem */
  char path[PATH_MAX] = "/";

  /* Pokud jsme odstranili adresář, v seznamu se nachazejí na přednějších příčkách
   * soubory obsažené v adresáři, pak teprve samotný adresář - proto úplně postačuje
   * odstranit IsoNody - není třeba volat iso_node_remove_tree() */
  for (FileList::const_iterator it = deleted->begin(); it != deleted->end(); ++it) {
    node = *it;
    strcpy(path+1, node->pathname);
    if (iso_tree_path_to_node(image, path, &iso_node) > 0)
      iso_node_remove(iso_node);
  }

  /* V tomto cyklu je nutno uvolnit uzly (FileNode*) v objektu files, neboť
   * tyto mohou obsahovat bufferovaná data, která mohou být duplikována v rámci
   * BufferStreamu - proto musí metoda bufferStreamFree vždy uvolnit buffer -
   * v objektu FileSystem pak není možnost jak zkontrolovat, zdali byl již Buffer
   * uvolněn.
   */
  for (FileMap::iterator it = files->begin(); it != files->end(); ++it) {
    node = it->second;

    /* Uzel nebyl změněn ani přejmenován */
    if (!node->changed && !node->original_pathname)
      goto for_end;

    /* Uzel je pouze přejmenován a nachází se v archivu */
    if (node->original_pathname && node->data) {
      strcpy(path+1, node->original_pathname);
      if ((ret = iso_tree_path_to_node(image, path, &iso_node)) == 1) {
        iso_node_set_name(iso_node, node->name_ptr);
      } else {
        cerr << "IsoDriver renaming failed: " << iso_error_to_msg(ret) << endl;
        goto for_end;
      }
    }

    /* Pokud nebyl obsah uzlu změněn, jsme s ním hotovi */
    if (!node->changed) goto for_end;
    else {
      /* Uzly nacházející se v kořenovém adresáři archivu mají path nastavenu na NULL.
       */
      if (node->pathname == node->name_ptr)
        iso_parent_node = iso_image_get_root(image);
      else {
        *(node->name_ptr-1) = '\0';
        strcpy(path+1, node->pathname);
        *(node->name_ptr-1) = '/';

        /* Nadřazený uzel bychom měli najít vždy - neboť asociativní pole je
        * uspořádané, takže jestli byl přidán nový adresář a do něj posléze
        * soubory, v tomto cyklu se dostaneme nejdříve k adresáři a poté až
        * k obsaženým souborům.
        */
        iso_tree_path_to_node(image, path, (IsoNode**)(&iso_parent_node));
      }


      if (node->type == FileNode::DIR_NODE) {
        iso_tree_add_new_dir(iso_parent_node, node->name_ptr, NULL);
      } else if (node->type == FileNode::FILE_NODE) {


        /*
         * Pokud má nastavený atribut data, pak se jedná o soubor, který se již v
         * archivu nachází - mělo by tedy dojít k jeho přepsání. Libisofs ovšem
         * nenabízí funkce pro přepis dat souboru uvnitř archivu, takže tento
         * soubor nejdříve smažu a pak přidám jako nový.
         */
        if (node->data != NULL) {
          strcpy(path+1, node->pathname);
          if (iso_tree_path_to_node(image, path, &iso_node) > 0)
            iso_node_remove(iso_node);
        }
        stream = new (malloc(sizeof(BufferStream))) BufferStream(node->buffer);
        ret = iso_tree_add_new_file(iso_parent_node, node->name_ptr,
                                    (IsoStream*)stream,
                                    (IsoFile**)&iso_node);
        if (ret < 0) {
          cerr << "IsoDriver adding failed: " << iso_error_to_msg(ret) << endl;
        }

        iso_node_set_permissions(iso_node, node->file_info.st_mode);
        iso_node_set_atime(iso_node, node->file_info.st_atime);
        iso_node_set_ctime(iso_node, node->file_info.st_ctime);
        iso_node_set_mtime(iso_node, node->file_info.st_mtime);
      }
    }

    for_end:
    // Nullování dat - již byla dříve dealokována
    it->second->data = NULL;
  }

  iso_write_opts_new(&wopts, 1);

  if (iso_image_create_burn_source(image, wopts, &burn_src) < 0) {
    cerr << "IsoDriver: cannot create burn source" << endl;
  }
  iso_write_opts_free(wopts);

  string output_name = archive_path;

  if (iso_source) {
    generateNewArchiveName(output_name);
  }
  ofstream image_file(output_name.c_str());
  if (!image_file.is_open()) {
    cerr << "IsoDriver: cannot open output file" << endl;
    return false;
  }

  while (burn_src->read_xt(burn_src, (unsigned char*)buf, buf_size) == buf_size) {
    image_file.write(buf, buf_size);
    if (image_file.bad()) {
      cerr << "IsoDriver: an error occured while writing to output file" << endl;
      return false;
    }
  }
  image_file.close();
  burn_src->free_data(burn_src);
  free(burn_src);

  iso_image_unref(image);

  if (!keep_original) {
    ::rename(output_name.c_str(), archive_path);
  }
//   iso_data_source_unref(iso_source);

//   for (vector<FileMap::iterator>::const_iterator it = nodes_to_free.begin();
//        it != nodes_to_free.end(); ++it) {
//     delete ((*it)->second);
//     files->erase(*it);
//   }
//   nodes_to_free.clear();

  return true;
}

int IsoDriver::bufferStreamOpen(IsoStream* stream) {
  if (stream == NULL) return ISO_NULL_POINTER;
  BufferStream* s = reinterpret_cast<BufferStream*>(stream);

  if (s->pos != -1) return ISO_FILE_ALREADY_OPENED;

  s->pos = 0;
  return ISO_SUCCESS;
}

int IsoDriver::bufferStreamClose(IsoStream* stream) {
  if (stream == NULL) return ISO_NULL_POINTER;
  BufferStream* s = reinterpret_cast<BufferStream*>(stream);

  if (s->pos == -1) return ISO_FILE_NOT_OPENED;

  s->pos = -1;
  return ISO_SUCCESS;
}

off_t IsoDriver::bufferStreamGetSize(IsoStream* stream) {
  if (stream == NULL) return ISO_NULL_POINTER;
  BufferStream* s = reinterpret_cast<BufferStream*>(stream);

  return off_t(s->buffer->length());
}

int IsoDriver::bufferStreamRead(IsoStream* stream, void* buf, size_t count) {
  if (stream == NULL || buf == NULL) return ISO_NULL_POINTER;
  BufferStream* s = reinterpret_cast<BufferStream*>(stream);

  if (count == 0) return ISO_WRONG_ARG_VALUE;
  if (s->pos == -1) return ISO_FILE_NOT_OPENED;

  int read_bytes = s->buffer->read((char*)buf, count, s->pos);
  s->pos += read_bytes;
  return read_bytes;
}

int IsoDriver::bufferStreamIsRepeatable(IsoStream* stream) {
  (void)stream;
  return 1;
}

void IsoDriver::bufferStreamGetId(IsoStream* stream, unsigned int* fs_id,
                                 dev_t* dev_id, ino_t* ino_id) {
  BufferStream* s = reinterpret_cast<BufferStream*>(stream);
  *fs_id = 4; // ISO_MEM_FS_ID (definováno v fsource.h z libisofs.h)
  *dev_id = 0;
  *ino_id = s->ino_id;
}

void IsoDriver::bufferStreamFree(IsoStream* stream) {
  BufferStream* s = reinterpret_cast<BufferStream*>(stream);
  if (s->is_duplicate)
    delete s->buffer;
//   delete s;
  return;
}

int IsoDriver::bufferStreamUpdateSize(IsoStream* stream) {
  (void)stream;
  return ISO_SUCCESS;
}

IsoStream* IsoDriver::bufferStreamGetInputStream(IsoStream* stream, int flag) {
  (void)stream;
  (void)flag;
  return NULL;
}

int IsoDriver::bufferStreamCmpIno(IsoStream* stream1, IsoStream* stream2) {
  return iso_stream_cmp_ino(stream1, stream2, 1);
}

int IsoDriver::bufferStreamCloneStream(IsoStream *old_stream,
                                       IsoStream **new_stream, int flag) {
  if (flag) return ISO_STREAM_NO_CLONE;

  BufferStream* old_s = reinterpret_cast<BufferStream*>(old_stream);
  BufferStream** new_s = reinterpret_cast<BufferStream**>(new_stream);

  Buffer* new_buf;
  try {
    new_buf = new Buffer(*(old_s->buffer));
  }
  catch (bad_alloc&) {
    return ISO_OUT_OF_MEM;
  }

  *new_s = new BufferStream(new_buf);
  (*new_s)->is_duplicate = true;
  return ISO_SUCCESS;
}

