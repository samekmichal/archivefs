/* Project:  ArchiveFS
 * Author:   Michal SAMEK
 * Email:    xsamek01@fit.vutbr.cz
 * Desc:     Implementation of buffer for storing file data
 * Modified: 4/2012
 */

#ifndef BUFFER_HPP
#define BUFFER_HPP

#include <cerrno>
#include <sys/types.h>

#include "filebuffer.hpp"
#include "membuffer.hpp"

class Buffer {
public:
  /// Definuje velikost segmentů bufferu
  /** Neboli počet bytů po kolika bude do buffery zapisováno */
  static const unsigned BLOCK_SIZE = 4*1024;

  /// Definuje mez pro velikost paměťového bufferu
  /** Po jejím překročení bude poměťový buffer zapsán do souborového bufferu
   *  a uvolněn. */
  static offset_t MEM_LIMIT;

  Buffer(offset_t size = 0) {
    if ((size > MEM_LIMIT && MEM_LIMIT > 0) || MEM_LIMIT == 0) {
      _buffer = new FileBuffer(size);
      _type = FILE;
    } else {
      _buffer = new MemBuffer(size);
      _type = MEM;
    }
  }

  Buffer(const Buffer& old) {
    _type = old._type;
    if (_type == MEM)
      _buffer = new MemBuffer(*(static_cast<MemBuffer*>(old._buffer)));
    else
      _buffer = new FileBuffer(*(static_cast<FileBuffer*>(old._buffer)));
  }

  /**
   * Destruktor dealokuje paměť užívanou bufferem.
   */
  ~Buffer() {
    delete _buffer;
  };

  /**
   * Funkce pro uvolnění a případnou dealokaci bufferu.
   * Pokud buffer sídlí v paměti je uvolněn a funkce vrací true.
   * Pokud sídlí na disku, není uvolněn a funkce vrací false.
   */
  bool release() {
    if (_type == MEM) {
      delete this;
      return true;
    }
    return false;
  }

  /**
   * Funkce čte do paměti odkazované ukazatelem buffer data o velikosti
   * bytes bytů s offsetem offset.
   *
   * Vždy se bude číst maximálně tolik bytů, kolik jich buffer obsahuje.
   * @returns počet přečtených bytů
   */
  size_t read(char* buffer, size_t length, offset_t offset) {
    return _buffer->read(buffer, length, offset);
  }

  /**
   * Funkce zapíše do bufferu data odkazovaná ukazatelem data o délce
   * length, na místo v bufferu s offsetem offset.
   *
   * Pokud je třeba, dojde automaticky ke zvětšení (doalokování)
   * místa v bufferu.
   * @returns počet zapsaných bytů
   */
  size_t write(const char* data, size_t length, offset_t offset) {
    offset_t total = offset + length;
    if (total > MEM_LIMIT && MEM_LIMIT > 0 && _type == MEM) {
      MemBuffer* mem_buf = static_cast<MemBuffer*>(_buffer);
      FileBuffer* file_buf = new FileBuffer(mem_buf->length());
      mem_buf->flushToFile(file_buf->getFd());
      _buffer = file_buf;
      _type = FILE;
      delete mem_buf;
    }
    return _buffer->write(data, length, offset);
  }

  void truncate(offset_t size) {
    //TODO: mem to file
    _buffer->truncate(size);
  }

  inline offset_t length() {
    return _buffer->length();
  }

private:
  BufferIface* _buffer;
  enum buffer_type {MEM, FILE} _type;
};



#endif
