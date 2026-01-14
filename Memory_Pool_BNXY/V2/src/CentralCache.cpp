#include "../include/CentralCache.h"
#include "../include/PageCache.h"
#include "../include/Common.h"
namespace Bnxy_memoryPool
{
    static const size_t SPAN_PAGES = 8;
    
    const std::chrono::milliseconds CentralCache::DELAY_INTERVAL{1000};
    //index:自由链表的索引，内存大小计算完后对应的位置

    CentralCache::CentralCache()
    {
        for (auto& freeList : centralFreeList_)
        {
            freeList.store(nullptr, std::memory_order_relaxed);
        }
        for (auto& lock : locks_)
        {
            lock.clear(std::memory_order_release);
        }
        for (auto& count : delayCounts_)
        {
            count.store(0, std::memory_order_relaxed);
        }
        for (auto& time : lastReturnTimes_)
        {
            time = std::chrono::steady_clock::now();
        }
        spanCount_.store(0, std::memory_order_relaxed);
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
            //记录这次申请的内存信息，通过span记录
            size_t trackerIndex = spanCount_++;
            if(trackerIndex < spanTrackers_.size())
            {
                spanTrackers_[trackerIndex].spanAddr.store(result, std::memory_order_release);
                spanTrackers_[trackerIndex].numPages.store(numPages, std::memory_order_release);
                spanTrackers_[trackerIndex].blockCount.store(blockNum, std::memory_order_release);
                spanTrackers_[trackerIndex].freeCount.store(blockNum - 1 , std::memory_order_release);
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
        // 1. 将归还的链表连接到中心缓存
        void* end = start;
        size_t count = 1;
        //遍历归还的链表，找到最后一个block的首地址地址
        while (*reinterpret_cast<void**>(end) != nullptr && count < blockCount) {
            end = *reinterpret_cast<void**>(end);
            count++;
        }
        void* current = centralFreeList_[index].load(std::memory_order_relaxed);
        *reinterpret_cast<void**>(end) = current; // 头插法（将原有链表接在归还链表后边）
        centralFreeList_[index].store(start, std::memory_order_release);
        
        // 2. 更新延迟计数
        size_t currentCount = delayCounts_[index].fetch_add(1, std::memory_order_relaxed) + 1;
        auto currentTime = std::chrono::steady_clock::now();
        
        // 3. 检查是否需要执行延迟归还
        if (shouldPerformDelayedReturn(index, currentCount, currentTime))
        {
            performDelayedReturn(index);
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
SpanTracker* CentralCache::getSpanTracker(void* addr)
{
    for(size_t i =0; i < spanCount_.load(std::memory_order_relaxed); ++i)
    {
        void* spanAddr = spanTrackers_[i].spanAddr.load(std::memory_order_relaxed);
        size_t numPages = spanTrackers_[i].numPages.load(std::memory_order_relaxed);
        if(addr >= spanAddr && addr < spanAddr + numPages * PageCache::PAGE_SIZE)//当前地址落位到一个span的内部（包含左边界）
        {
            return &spanTrackers_[i];
        }
    }
    return nullptr;
}
//通过累计次数和时间间隔判断是否需要延迟归还
bool CentralCache::shouldPerformDelayedReturn(size_t index, size_t currentCount, std::chrono::steady_clock::time_point currentTime)
{
  if (currentCount >= MAX_DELAY_COUNT)
    {
        return true;
    }
    
    auto lastTime = lastReturnTimes_[index];
    return (currentTime - lastTime) >= DELAY_INTERVAL;
}

//归还给Page
void CentralCache::performDelayedReturn(size_t index)
{
    //延迟归还的计数器重置一项
    delayCounts_[index].store(0, std::memory_order_relaxed);
    // 更新最后归还时间
    lastReturnTimes_[index] = std::chrono::steady_clock::now();
    // 统计每个span的空闲块数,每个size的block对应会有多个span，需要区分
    std::unordered_map<SpanTracker*, size_t> spanFreeCounts;
    void* currentBlock = centralFreeList_[index].load(std::memory_order_relaxed);
    while (currentBlock != nullptr)
    {
        SpanTracker* tracker = getSpanTracker(currentBlock);
        if (tracker)
        {
            spanFreeCounts[tracker]++;
        }
        currentBlock = *reinterpret_cast<void**>(currentBlock);
    }

    for(const auto&[tracker,freeCount]:spanFreeCounts)
    {
        updateSpanFreeCount(tracker, freeCount, index);
    }
}
void CentralCache::updateSpanFreeCount(SpanTracker* tracker, size_t newFreeBlocks, size_t index)
{   
    //更新每个span的空闲块数
    tracker->freeCount.store(newFreeBlocks, std::memory_order_release);
    //检查这个span是不是已经全部清空了
    if(newFreeBlocks == tracker->blockCount.load(std::memory_order_relaxed))
    {
        //如果span的空闲块数等于span的总块数，说明span已经全部清空了
        //准备归还
        void* spanAddr = tracker->spanAddr.load(std::memory_order_relaxed);
        size_t numPages = tracker->numPages.load(std::memory_order_relaxed);
        //从自己的链表里面找到这片span
        void* head = centralFreeList_[index].load(std::memory_order_relaxed);
        //从中心缓存的自由链表中删除这一片span
        //将这一片span从中心缓存的自由链表中删除，但要注意的是，必选返回一整块，而不是分割好的，所以要清空之前的首地址操作
        void* newHead = nullptr;
        void* pre = nullptr;
        void* current = head;
        while(current){
            void* next = *reinterpret_cast<void**>(current);
            if (current >= spanAddr && 
                current < static_cast<char*>(spanAddr) + numPages * PageCache::PAGE_SIZE)
            {
                if (pre)
                {
                    *reinterpret_cast<void**>(pre) = next;
                }
                else
                {
                    newHead = next;
                }
            }
            else
            {
                pre = current;
            }
            current = next;
        }
        centralFreeList_[index].store(newHead, std::memory_order_release);
        PageCache::getInstance().deallocateSpan(spanAddr, numPages);
    }
}

}//namespace Bnxy_memoryPool

