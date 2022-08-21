#include "mmap.hpp"
#include <mio/mmap.hpp>

#include <cassert>
#include <unordered_set>

struct MemoryMap_t {
  std::string path;
  mio::mmap_source src;
};

#if !defined(NDEBUG)
std::mutex gHandlesLock;
std::unordered_set<MemoryMapHandle> gHandles;
#endif

MemoryMapStatus Mmap_Open(MemoryMapHandle &out, const std::string &path) {
  if (path.empty()) {
    return Mmap_Failure;
  }

  mio::mmap_source src;

  auto *ret = new MemoryMap_t;
  if (!ret) {
    return Mmap_Failure;
  }

  ret->path = path;
  ret->src = std::move(src);
  out = ret;

#if !defined(NDEBUG)
  {
    std::lock_guard G(gHandlesLock);
    gHandles.insert(ret);
  }
#endif

  return Mmap_OK;
}

MemoryMapStatus Mmap_Map(const void *&buf,
                         size_t &out_len,
                         MemoryMapHandle file,
                         size_t offset,
                         size_t len) {
  if (!file) {
    return Mmap_InvalidHandle;
  }

  if (file->src.is_mapped()) {
    return Mmap_AlreadyMapped;
  }

  std::error_code rc;
  if (offset == 0 && len == 0) {
    file->src.map(file->path, rc);
  } else {
    file->src.map(file->path, offset, len, rc);
  }

  if (rc) {
    printf("mmap failure '%s' rc=%d\n", file->path.c_str(), rc.value());
    return Mmap_Failure;
  }

  buf = file->src.data();
  out_len = file->src.size();

  return Mmap_OK;
}

MemoryMapStatus Mmap_Unmap(MemoryMapHandle file) {
  if (!file) {
    return Mmap_InvalidHandle;
  }

  if (!file->src.is_mapped()) {
    return Mmap_NotMapped;
  }

  file->src.unmap();
  return Mmap_OK;
}

MemoryMapStatus Mmap_Close(MemoryMapHandle &file) {
  if (!file) {
    return Mmap_InvalidHandle;
  }
#if !defined(NDEBUG)
  {
    std::lock_guard G(gHandlesLock);
    gHandles.erase(file);
  }
#endif

  delete file;
  file = nullptr;
  return Mmap_OK;
}

MemoryMapStatus Mmap_CheckLeaks() {
#if defined(NDEBUG)
  return Mmap_OK;
#else
  if (gHandles.size() > 0) {
    for (auto &handle : gHandles) {
      fmt::print("[mmap] leaked path='{}' is_mapped={}\n", handle->path,
                 handle->src.is_mapped());
    }
    return Mmap_Failure;
  }

  return Mmap_OK;
#endif
}
