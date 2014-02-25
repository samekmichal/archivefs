/* Project:  ArchiveFS
 * Author:   Michal SAMEK
 * Email:    xsamek01@fit.vutbr.cz
 * Desc:     Interface for used buffers
 * Modified: 04/2012
 */

#ifndef BUFFER_IFACE_HPP
#define BUFFER_IFACE_HPP

#include <sys/types.h>

#ifdef _LFS_LARGEFILE
typedef off64_t offset_t;
#else
typedef off_t offset_t;
#endif


struct BufferIface {
  virtual ~BufferIface() {}
  virtual size_t read(char*, offset_t, offset_t) const = 0;
  virtual size_t write(const char*, offset_t, offset_t) = 0;
  virtual void truncate(offset_t) = 0;
  virtual offset_t length() = 0;
};

#endif
