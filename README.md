# ArchiveFS

ArchiveFS is a tool for creating virtual file systems (using FUSE framework) from
archive files.

Current implementation supports ZIP(read/write), ISO(read/write), TAR(read), TGZ(read).


## Compilation and instalation
Just follow classic scenario:
  ./configure
  make
  make install

By default the installation will place the archivefs files to /usr/local directory.
This can be changed with --prefix argument.


This project is dependent on these libraries:
* libfuse
* libzip
* libisofs
* libtar
* libmagic (optional)
* C++/BOOST (boost/algorithm/string/predicate.hpp)


## How to run it
As a source for virtual filesystem you can use archive file or entire folder
containing archive files.

  $ archivefs <archive> <mountpoint>
  $ archivefs <folderWithArchives> <mountpoint>

Data are written to archives while unmounting the file system. Especially writing
lots of data can consume significant amount of time - always make sure if archivefs
has already exited (man ps | grep "archivefs").


## Drivers
Archivefs needs its drivers for manipulation with archive files. These drivers
can be added dynamicaly during runtime. By default these drivers reside in
directory /usr/local/lib (to use different drivers storage use --drivers-path
during startup of archivefs).


## Buffering
Default behaviour of archivefs involves buffering data read from archive. By
using argument --buffer-limit you can specify how much memory can be used for
buffer for one file.


## Erasing files in archives
If you erase files in virtual filesystem, they are by default copied to trash
directory belonging to the filesystem (in case of archive file, this directory
resides inside the archive). Since we don't want to keep deleted files inside
the archive, this trash directory is deleted unless specified by using swich
--keep-trash.


## Archive creation
Archives with write support (ZIP) can be created and immediately mounted when
using option --create.
