#pragma once

#include "leveldb/cache.h"

namespace leveldb {

static int dummy = 0;

class NullCache : public Cache {
  public:
    NullCache();
    ~NullCache();

    Handle* Insert(const Slice& key, void* value, size_t charge,
                         void (*deleter)(const Slice& key, void* value));
    Handle* Lookup(const Slice& key);
    void Release(Handle* handle);
    void* Value(Handle* handle);
    void Erase(const Slice& key);
    uint64_t NewId();
    size_t TotalCharge() const;
};

NullCache::NullCache() {
  return;
}

NullCache::~NullCache() {
  return;
}

NullCache::Handle* NullCache::Insert(const Slice& key, void* value, size_t charge,
                     void (*deleter)(const Slice& key, void* value)) {
  return reinterpret_cast<Cache::Handle*>(&dummy);
}

NullCache::Handle* NullCache::Lookup(const Slice& key) {
  return nullptr;
}

void NullCache::Release(Handle* handle) {
  return;
}

void* NullCache::Value(Handle* handle) {
  return &dummy;
}

void NullCache::Erase(const Slice& key) {
  return;
}

uint64_t NullCache::NewId() {
  return 0;
}

size_t NullCache::TotalCharge() const {
  return 0;
}

}
