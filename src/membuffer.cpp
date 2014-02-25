/* Project:  ArchiveFS
 * Author:   Michal SAMEK
 * Email:    xsamek01@fit.vutbr.cz
 * Desc:     File implementing buffer allocated on the heap.
 * Modified: 04/2012
 */

#include <cerrno>
#include "membuffer.hpp"

MemBuffer::Chunk::Chunk(bool initialize) {
  data = new char[CHUNK_SIZE];
  if (data == NULL)
    throw std::bad_alloc();

  if (initialize) memset(data, 0, sizeof(char)*CHUNK_SIZE);
}

MemBuffer::Chunk::Chunk(char* _data, size_t _len) {
  data = new char[CHUNK_SIZE];
  if (data == NULL)
    throw std::bad_alloc();

  if (_len > CHUNK_SIZE) _len = CHUNK_SIZE;

  memcpy(data, _data, _len);
}

MemBuffer::Chunk::~Chunk() {
  delete[] data;
  data = NULL;
}

size_t MemBuffer::Chunk::read(char* _data, size_t len, offset_t offset) {
  unsigned bytes_to_read;
  if (len + offset > CHUNK_SIZE) {
    bytes_to_read = CHUNK_SIZE-offset;
    memcpy(_data, data+offset, bytes_to_read);
  } else {
    bytes_to_read = len;
    memcpy(_data, data+offset, bytes_to_read);
  }
  return bytes_to_read;
}

size_t MemBuffer::Chunk::write(const char* _data, size_t len, offset_t offset) {
  unsigned bytes_to_write;
  if (len + offset > CHUNK_SIZE)
    bytes_to_write = CHUNK_SIZE-offset;
  else
    bytes_to_write = len;
  memcpy(data+offset, _data, bytes_to_write);
  return bytes_to_write;
}



MemBuffer::MemBuffer(unsigned len) {
  _length = len;
  chunks.clear();
  for (unsigned i = 0; i < chunksCount(len); ++i) {
    chunks.push_back(new Chunk(true));
  }
}

MemBuffer::MemBuffer(const MemBuffer& old) {
  _length = old._length;
  MemBuffer::Chunk* ch;
  for (unsigned i = 0; i < chunksCount(_length); ++i) {
    try {
      ch = new Chunk;
    }
    catch (bad_alloc&) {
      for (unsigned j = 0; j < i; ++j) {
        delete chunks[j];
      }
      throw;
    }
    chunks.push_back(ch);
    memcpy(chunks[i]->data, old.chunks[i]->data, CHUNK_SIZE);
  }
}

// MemBuffer::MemBuffer(char* data, ssize_t len) {
//   length = len;
//   off_t offset = 0;
//
//   for (unsigned i = 0; i < chunksCount(length); ++i) {
//     Chunk* a = new Chunk(data+offset, length);
//     offset += CHUNK_SIZE;
//     chunks.push_back(a);
//   }
// }

MemBuffer::~MemBuffer() {
  if (_length == 0) return;
  for (vector<Chunk*>::iterator it = chunks.begin(); it != chunks.end(); ++it) {
    delete (*it);
  }
}

char* MemBuffer::getChunkData(unsigned i) {
  return chunks.at(i)->data;
}

char* MemBuffer::operator[](int i) {
  if (i < 0) i = _length - i;
  return chunks[i]->data;
}

size_t MemBuffer::read(char* buffer, offset_t bytes, offset_t offset) const {
  size_t read = 0;
  if (offset > _length) return -EINVAL;
  if (bytes+offset > _length) bytes = _length-offset;
  size_t bytes_to_read = bytes;
  unsigned chunk_start = chunkNumber(offset);
  unsigned chunk_end = chunkNumber(offset) + chunksCount(bytes);

  for (unsigned i = chunk_start; i < chunk_end; ++i) {
    read = chunks[i]->read(buffer, bytes_to_read, chunkOffset(offset));
    offset += read;
    buffer += read;
    bytes_to_read -= read;
  }
  return bytes;
}

size_t MemBuffer::write(const char* data, offset_t data_len, offset_t offset) {
  unsigned chunks_needed = chunksCount(data_len + offset);
  unsigned chunks_count = chunks.size();

  if (chunks_count < chunks_needed) {
    unsigned new_chunks = chunks_needed - chunks_count;
    for (unsigned i = 0; i < new_chunks; ++i) {
      chunks.push_back(new Chunk);
    }
  }
  _length = offset;

  unsigned chunks_to_write = chunksCount(data_len);
  unsigned chunk_start = chunkNumber(offset);
  unsigned chunk_end = chunkNumber(offset)+chunks_to_write;
  size_t written = 0;
  for (unsigned i = chunk_start; i < chunk_end; ++i) {
    written = chunks[i]->write(data+written, data_len-written, chunkOffset(offset));
    offset += written;
    _length += written;
  }
  return written;
}

void MemBuffer::truncate(offset_t size) {
  if (size < _length) {
    memset(getChunkData(chunkNumber(size))+chunkOffset(size), 0, CHUNK_SIZE-size);
    for (unsigned chunk = chunksCount(size); chunk < chunks.size(); ++chunk)
      delete chunks.at(chunk);
    chunks.resize(chunksCount(size));
  } else {
    for (unsigned i = chunks.size(); i < chunksCount(size); ++i)
      chunks.push_back(new Chunk(true));
  }

  _length = size;
}

bool MemBuffer::flushToFile(int fd) {
  offset_t bytes_to_write = _length;
  for (uint i = 0; i < chunksCount(_length); ++i) {
    bytes_to_write -= ::write(fd, chunks[i]->data,
                             (bytes_to_write < CHUNK_SIZE?bytes_to_write:CHUNK_SIZE));
  }
  if (bytes_to_write == 0)
    return true;
  else
    return false;
}

// void MemBuffer::print() {
//   for (vector<Chunk*>::iterator it = chunks.begin(); it != chunks.end(); ++it) {
//     cout.write((*it)->data, CHUNK_SIZE);
//   }
// }

ostream& operator<< (ostream& stream, MemBuffer& buffer) {
  char* out_buf = new char[buffer._length];
  buffer.read(out_buf, buffer._length);
  stream.write(out_buf, buffer._length);
  delete[] out_buf;
  return stream;
}
