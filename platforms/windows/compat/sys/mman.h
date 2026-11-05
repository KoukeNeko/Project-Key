// Read-only mmap for McBopomofo's MemoryMappedFile on MSVC, which has no
// POSIX mapping API. Only what MemoryMappedFile.cpp calls is provided.
#pragma once

#include <io.h>
#include <stddef.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#define PROT_READ 1
#define MAP_SHARED 1
#define MAP_FAILED ((void*)-1)

inline void* mmap(void*, size_t length, int, int, int fd, long long offset) {
  HANDLE file = (HANDLE)_get_osfhandle(fd);
  if (file == INVALID_HANDLE_VALUE || length == 0) return MAP_FAILED;
  HANDLE mapping = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
  if (!mapping) return MAP_FAILED;
  void* view = MapViewOfFile(mapping, FILE_MAP_READ, (DWORD)(offset >> 32), (DWORD)offset, length);
  CloseHandle(mapping);  // the view keeps the mapping alive
  return view ? view : MAP_FAILED;
}

inline int munmap(void* address, size_t) {
  return UnmapViewOfFile(address) ? 0 : -1;
}
