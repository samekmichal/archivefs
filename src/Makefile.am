## Process this file with automake to produce Makefile.in

bin_PROGRAMS = archivefs

archivefs_SOURCES =    \
  archivefs.cpp  \
  membuffer.cpp  \
  filenode.cpp   \
  filesystem.cpp \
  drivers.cpp
archivefs_CXXFLAGS = -D 'RPATH="@libdir@"'
archivefs_LDFLAGS = -pthread -ldl -rdynamic -Wl,-rpath=@libdir@

