/* Project:  ArchiveFS
 * Author:   Michal SAMEK
 * Email:    xsamek01@fit.vutbr.cz
 * Desc:     Wrapper for tardriver with GZIP compression
 * Modified: 04/2012
 */

#include "tgzdriver.hpp"

extern "C" {
  ArchiveDriver* maker(const char* archive, bool create_archive) {
    return new TarGzDriver(archive, create_archive);
  }

  DriverHandle* REGISTER_DRIVER () {
    return new DriverHandle("tgz", maker);
  }
}