#pragma once
#include "ThreadCache.h"

namespace Bnxy_memoryPool
{

class MemoryPool
{
public:
    static void* allocate(size_t size)
    {
        return ThreadCache::GetInstance()->allocate(size);
    }

    static void deallocate(void* ptr, size_t size)
    {
        ThreadCache::GetInstance()->deallocate(ptr, size);
    }
};

} // namespace Bnxy_memoryPool
