/* Project:  ArchiveFS
 * Author:   Michal SAMEK
 * Email:    xsamek01@fit.vutbr.cz
 * Desc:     Header file for tardriver.cpp
 * Modified: 04/2012
 */

#ifndef TAR_GZ_DRIVER_HPP
#define TAR_GZ_DRIVER_HPP

#include "tardriver.hpp"

class TarGzDriver: public TarDriver {
public:
  TarGzDriver(const char* _archive, bool create_archive)
    : TarDriver(_archive, create_archive, TarDriver::GZIP) {};
};

#endif