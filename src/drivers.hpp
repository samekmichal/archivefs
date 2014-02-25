/* Project:  ArchiveFS
 * Author:   Michal SAMEK
 * Email:    xsamek01@fit.vutbr.cz
 * Desc:     Header file for drivers.cpp
 *           - implementation of runtime drivers loading
 * Modified: 04/2012
 */

#ifndef DRIVERS_HPP
#define DRIVERS_HPP

#include <dlfcn.h>
#include <map>
#include <vector>
#include "archivedriver.hpp"

#include <boost/algorithm/string/predicate.hpp>
#define ENDS_WITH(STRING, ENDING) \
        (boost::algorithm::ends_with(STRING, ENDING))
#define STARTS_WITH(STRING, STARTING) \
        (boost::algorithm::starts_with(STRING, STARTING))

typedef vector<DriverHandle*> DriversVector;
typedef DriverHandle* (*regdr_ptr) ();

const char* findFileExt(const char* path, const char* ext);
bool LOAD_STANDARD_DRIVERS();
bool LOAD_DRIVER(const char* pathname);
void UNLOAD_DRIVERS();
ArchiveType* GET_TYPE(const char* path);
ArchiveType* TYPE_BY_EXT(const char* ext);
ArchiveType* TYPE_BY_MIME(const char* mime);
void PRINT_DRIVERS_SUPPORT();
void generateNewArchiveName(string& name);

#endif