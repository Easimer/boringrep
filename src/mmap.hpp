#pragma once

#include <optional>
#include <string>

typedef struct MemoryMap_t *MemoryMapHandle;

enum MemoryMapStatus {
  Mmap_OK,
  Mmap_Failure,
  Mmap_InvalidHandle,
  Mmap_AlreadyMapped,
  Mmap_NotMapped,
};
	

MemoryMapStatus Mmap_Open(MemoryMapHandle &out, const std::string &path);
MemoryMapStatus Mmap_Map(const void *&buf,
                         size_t &out_len,
                         MemoryMapHandle file,
                         size_t offset = 0,
                         size_t len = 0);
MemoryMapStatus Mmap_Unmap(MemoryMapHandle file);
MemoryMapStatus Mmap_Close(MemoryMapHandle &file);

MemoryMapStatus Mmap_CheckLeaks();
