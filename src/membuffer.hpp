/* Project:  ArchiveFS
 * Author:   Michal SAMEK
 * Email:    xsamek01@fit.vutbr.cz
 * Desc:     Header file for buffer.cpp
 * Modified: 4/2012
 */

#ifndef MEM_BUFFER_HPP
#define MEM_BUFFER_HPP

#include <vector>
#include <cstdlib>
#include <cstring>
#include <iostream>

#include "bufferiface.hpp"

using namespace std;

/// Dynamicky se zvětšující buffer
/** \class MemBuffer
 * Implementuje dynamický buffer na haldě, alokující své části o velikosti
 * definované v konstantě CHUNK_SIZE.
 */
class MemBuffer: public BufferIface {
public:
  /// Velikost jednoho "dílu" bufferu - najednou alokované jednotky
  static const unsigned CHUNK_SIZE = 4*1024;

  /**
   * Alokuje pameť bufferu pro uložení len bytů.
   * @throws std_bad_alloc pokud není možno alokovat další paměť
   */
  MemBuffer(unsigned len = 0);

  MemBuffer(const MemBuffer& old);

  /**
   * Destruktor dealokuje paměť užívanou bufferem.
   */
  ~MemBuffer();

  /**
   * Funkce čte do paměti odkazované ukazatelem buffer data o velikosti
   * bytes bytů s offsetem offset.
   *
   * Vždy se bude číst maximálně tolik bytů, kolik jich buffer obsahuje.
   * @returns počet přečtených bytů
   */
  size_t read(char* buffer, offset_t bytes, offset_t offset = 0) const;

  /**
   * Funkce zapíše do bufferu data odkazovaná ukazatelem data o délce
   * length, na místo v bufferu s offsetem offset.
   *
   * Pokud je třeba, dojde automaticky ke zvětšení (doalokování)
   * místa v bufferu.
   * @returns počet zapsaných bytů
   */
  size_t write(const char* data, offset_t length, offset_t offset = 0);

  void truncate(offset_t size);

  inline offset_t length() {
    return _length;
  }

  bool flushToFile(int fd);

  friend ostream& operator<< (ostream&, MemBuffer&);

private:
  /** \class MemBuffer::Chunk
   * Implementuje segment bufferu.
   */
  class Chunk {
  public:
    /// Ukazatel na data segmentu.
    char* data;

    /**
     * Konstruktor alokuje místo pro uložení dat.
     * Lze specifikovat, zdali se má paměť s daty inicializovat = vynulovat.
     * @throws std::bad_alloc pokud není možno alokovat další paměť
     */
    Chunk(bool initialize = false);

    /**
     * Konstruktor alokuje místo pro uložení dat a kopíruje _len bytů
     * z dat odkazovaných ukazatelem _data - NEJVÍCE však CHUNK_SIZE bytů.
     * @throws std::bad_alloc pokud není možno alokovat další paměť
     */
    Chunk(char* _data, size_t _len = CHUNK_SIZE);

    /**
     * Destruktor uvolní paměť užívanou segmentem, nuluje ukazetel data.
     */
    ~Chunk();

    /**
     * Funkce čte len bytů segmentu s offsetem offset do paměti odkazované
     * ukazatelem _data.
     *
     * Pokud by se při čtení len bytů s daným offsetem mělo přistoupit za
     * hranici segmentu, přečte se pouze zbytek segmentu od začátku offsetu.
     * @returns počet přečtených bytů
     */
    size_t read(char* _data, size_t len, offset_t offset);

    /**
     * Funkce zapíše len bytů dat z paměti odkazované ukazatelem _data do
     * datové části segmentu bufferu s offsetem offset.
     *
     * Pokud by zápis len bytů s offsetem offset měl zapsat za hranici segmentu
     * bude zapsáno pouze tolik bytů, aby k tomuto překročení nedošlo.
     * @returns počet zapsaných batů
     */
    size_t write(const char* _data, size_t len, offset_t offset);
  };

  offset_t _length;

  /// Vektor se segmenty bufferu.
  vector<Chunk*> chunks;

  /**
   * Vrací ukazatel na užitečná data i-tého segmentu bufferu.
   * @throws std::out_of_range při překročení hranic bufferu
   */
  char* getChunkData(unsigned i);

  /**
   * Slouží k indexaci segmentů.
   * Nevyvolává vyjímky při překročení hranic bufferu.
   * Pokud je index záporný, počítají se segmenty do konce bufferu.
   */
  char* operator[](int i);

  /// Vrací počet segmentů potřebných k uložení len bytů.
  inline static unsigned chunksCount(offset_t len) {
    return (len + CHUNK_SIZE -1) / CHUNK_SIZE;
  }

  /// Vrací index segmentu obsahujícího první byte s offsetem offset.
  inline static unsigned chunkNumber(offset_t offset) {
    return offset/CHUNK_SIZE;
  }

  /// Vrací offset v segmentu pro byte se segmentem offset v bufferu.
  inline static unsigned chunkOffset(offset_t offset) {
    return offset%CHUNK_SIZE;
  }
};


#endif