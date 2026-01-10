#include "../include/CentralCache.h"
#include "../include/Common.h"
namespace Bnxy_memoryPool
{
    static const size_t SPAN_PAGES = 8;
    //index:自由链表的索引，内存大小计算完后对应的位置
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
    // 使用try-catch来保证不会直接throw出异常，而是直接返回nullptr保护
    try
    {
        result = centralFreeList_[index].load(std::memory_order_relaxed);//尝试从自己的自由链表里拿出来
        if(!result)//如果对应的缓冲为空
        {
            //从Page里面申请
            size_t size = (index + 1) * ALIGNMENT;
            size_t numPages;
            result = GetFromPageCache(size, numPages);//从PageCache里面申请,现在拿到的是一整块，要分割
            if (!result)//没拿到
            {
                locks_[index].clear(std::memory_order_release);
                return nullptr;
            }
            char* start = reinterpret_cast<char*>(result);

            size_t blockNum = (numPages * PageCache::PAGE_SIZE) / size;//要分块的数量
            if(blockNum > 1)
            {  
                for(size_t i = 0;i < blockNum - 1;++i)
                {
                    *reinterpret_cast<void**>(start + i * size) = start + (i + 1) * size;//这里就是分块了，已经按照需要的大小size做了一块一块的分割
                    //同时还把每一个block的前面void*大小的内存也设置为下一个block的地址，
                    // 这样就可以通过void*来遍历整个自由链表了
                }
                *reinterpret_cast<void**>(start + (blockNum - 1) * size) = nullptr;
                
                void* next = *reinterpret_cast<void**>(result);//记录第一个block的地址
                //把第一个block作为result返回给Thread，然后断开链表
                *reinterpret_cast<void**>(result) = nullptr;
                centralFreeList_[index].store(next, std::memory_order_release);//把链表头的地址更新成下一个
            }
            
        }
        else{//里面有能用的，分配给Thread
            void* next = *reinterpret_cast<void**>(result);//这是下一个
            *reinterpret_cast<void**>(result) = nullptr;//断开reslut和下一个（头删）

            //更新自由链表
            centralFreeList_[index].store(next, std::memory_order_release);//把链表头的地址更新成下一个
            //更新span的计数
            SpanTracker* tracker = getSpanTracker(result);
            if (tracker)
            {
                // 减少一个空闲块
                tracker->freeCount.fetch_sub(1, std::memory_order_release);
            }

        }
    }
    catch (...) 
    {
        locks_[index].clear(std::memory_order_release);
        throw;
    }
    locks_[index].clear(std::memory_order_release);
    return result;
}
void CentralCache::returnRange(void* start, size_t size, size_t index){
    
}

void* CentralCache::GetFromPageCache(size_t size ,size_t& numPages)
{   
    // 1. 计算实际需要的页数
    size_t numPages = (size + PageCache::PAGE_SIZE - 1) / PageCache::PAGE_SIZE;

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
}//namespace Bnxy_memoryPool

