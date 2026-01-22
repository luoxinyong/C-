#include "../include/CentralCache.h"
#include "../include/PageCache.h"
#include "../include/Common.h"
#include <algorithm>
namespace Bnxy_memoryPool
{
    static const size_t SPAN_PAGES = 8;
    
    //index:自由链表的索引，内存大小计算完后对应的位置

    CentralCache::CentralCache()
    {
        
        for (auto& lock : locks_)
        {
            lock.clear(std::memory_order_release);
        }
    }
void* CentralCache::fetchRange(size_t index){
    //申请过大不让分配，直接从系统要把
    if(index >= FREE_LIST_SIZE)
    {
        return nullptr;
    }
    //要先拿到这个锁对自由链表进行操作，后面所有的读写操作必须在acquire之后，通过while一直自旋
    while (locks_[index].test_and_set(std::memory_order_acquire)) 
    {
        std::this_thread::yield();
    }
    void* result = nullptr;//准备一个空指针，用于返回结果
    //第一步还是从自己的自由链表里面尝试给一块内存分割好后交给Thread使用
    try
    {
        SpanTracker* span = centralSpanFreeList_[index];
        while(span){
            if(span->freeList != nullptr)
            {
                break;
            }
            span = span->next;
        }
       if(!span){   
            size_t userSize = (index + 1) * ALIGNMENT;
            size_t realBlocksize = SizeClass::roundUp(userSize + sizeof(BlockHeader_));
            size_t numPages;
            result  = GetFromPageCache(realBlocksize, numPages);
            if(!result){
                locks_[index].clear(std::memory_order_release);
                return nullptr;
            }
            char* start = reinterpret_cast<char*>(result);
            size_t blockNum = (numPages * PageCache::PAGE_SIZE) / realBlocksize;
            if(blockNum == 0){
                locks_[index].clear(std::memory_order_release);
                return nullptr;
            }
            SpanTracker* Newspan = new SpanTracker();
            Newspan->spanAddr.store(start, std::memory_order_relaxed);
            Newspan->numPages.store(numPages, std::memory_order_relaxed);
            Newspan->blockSize.store(realBlocksize, std::memory_order_relaxed);
            Newspan->blockCount.store(blockNum, std::memory_order_relaxed);
            Newspan->freeCount.store(blockNum, std::memory_order_relaxed);
            Newspan->freeList = nullptr;

            BlockHeader_* prev = nullptr;
            for(size_t i = 0; i < blockNum; i++)
            {
                BlockHeader_* block = reinterpret_cast<BlockHeader_*>(start + i * realBlocksize);
                block->next = prev;
                block->span = Newspan;
                prev = block;
            }
             Newspan->freeList = prev;
             Newspan->next = centralSpanFreeList_[index];
             centralSpanFreeList_[index] = Newspan;

             BlockHeader_* returnblock = Newspan->freeList;
             Newspan->freeList = returnblock->next;
             Newspan->freeCount.fetch_sub(1, std::memory_order_relaxed);
             
             result = returnblock;
        }
        else{
        BlockHeader_* block = span->freeList;
        span->freeList = block ->next;
        span->freeCount.fetch_sub(1, std::memory_order_relaxed);
        result = block;
        }
    }
    catch(...)
    {
        locks_[index].clear(std::memory_order_release);
        throw;
    }
    locks_[index].clear(std::memory_order_release);   
    return reinterpret_cast<char*>(result) + sizeof(BlockHeader_);
}
void CentralCache::returnRange(void* start, size_t size, size_t index){
    if (!start || index >= FREE_LIST_SIZE) 
        return;

    size_t blockSize = (index + 1) * ALIGNMENT;
    size_t blockCount = size / blockSize;    

    while (locks_[index].test_and_set(std::memory_order_acquire)) 
    {
        std::this_thread::yield();
    }
    try 
    {
        //归还的内存首地址，偏移至对应的BlockHeader
        BlockHeader_* cur = reinterpret_cast<BlockHeader_*>(static_cast<char*>(start) - sizeof(BlockHeader_));
        while(cur){
            //开始遍历整个归还的链表，将每个block插入到对应的span的自由链表中
            BlockHeader_* next = cur->next;//下一块内存
            SpanTracker* span = cur->span;//当前内存对应的span

            cur->next = span->freeList;//头插插到对应的链表
            span->freeList = cur;
            span->freeCount.fetch_add(1, std::memory_order_relaxed);

            if (span->freeCount.load(std::memory_order_relaxed) ==
                span->blockCount.load(std::memory_order_relaxed)) {
                updateSpanFreeCount(span, index);
            }

            cur = next;
        }
    }
    catch (...) 
    {
        locks_[index].clear(std::memory_order_release);
        throw;
    }

    locks_[index].clear(std::memory_order_release);
}

void* CentralCache::GetFromPageCache(size_t size ,size_t& numPages)
{   
    // 1. 计算实际需要的页数
    numPages = (size + PageCache::PAGE_SIZE - 1) / PageCache::PAGE_SIZE;

    // 2. 根据大小决定分配策略
    if (size <= SPAN_PAGES * PageCache::PAGE_SIZE) 
    {
        // 小于等于32KB的请求，使用固定8页
        return PageCache::getInstance().allocateSpan(SPAN_PAGES);
    } 
    else 
    {
        // 大于32KB的请求，按实际需求分配
        return PageCache::getInstance().allocateSpan(numPages);
    }
}

//通过累计次数和时间间隔判断是否需要延迟归还

void CentralCache::updateSpanFreeCount(SpanTracker* span,  size_t index)
{   
    //更新每个span的空闲块数
   SpanTracker* prev = nullptr;
    SpanTracker* cur  = centralSpanFreeList_[index];

    while (cur) {
        if (cur == span) {
            if (prev) {
                prev->next = cur->next;
            } else {
                centralSpanFreeList_[index] = cur->next;
            }
            break;
        }
        prev = cur;
        cur = cur->next;
    }
        
        PageCache::getInstance().deallocateSpan(span->spanAddr.load(std::memory_order_relaxed),
        span->numPages.load(std::memory_order_relaxed));
        delete span;
}



}//namespace Bnxy_memoryPool

