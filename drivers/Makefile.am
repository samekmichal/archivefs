## Process this file with automake to produce Makefile.in

AM_CPPFLAGS= -I../src

lib_LTLIBRARIES = afs_zipdriver.la afs_isodriver.la afs_tardriver.la

afs_zipdriver_la_SOURCES = zipdriver.cpp
afs_zipdriver_la_LDFLAGS = -lzip -module

afs_isodriver_la_SOURCES = isodriver.cpp
afs_isodriver_la_LDFLAGS = -lisofs -module

afs_tardriver_la_SOURCES = tardriver.cpp
afs_tardriver_la_LDFLAGS = -ltar -module

