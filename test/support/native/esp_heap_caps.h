#pragma once
#include <Arduino.h>
constexpr unsigned MALLOC_CAP_INTERNAL=1, MALLOC_CAP_8BIT=2;
inline void *heap_caps_aligned_calloc(size_t alignment, size_t count, size_t size, unsigned) {
    if (fake::allocation_failure) return nullptr;
    void *p=nullptr;
    if (posix_memalign(&p, alignment, count*size)) return nullptr;
    memset(p, 0, count*size); ++fake::allocations; return p;
}
inline void heap_caps_free(void *p) { if (p) { ++fake::frees; free(p); } }
