#include "../include/PageCache.h"
#include "../include/Common.h"
#include <sys/mman.h>
namespace Bnxy_memoryPool
{
    
        // 分配指定页数的span
void* PageCache::allocateSpan(size_t numPages){
    std::lock_guard<std::mutex> lock(mutex_);
    // 查找合适的空闲span
    // lower_bound函数返回第一个大于等于numPages的元素的迭代器
    auto it = freeSpans_.lower_bound(numPages);
    if (it != freeSpans_.end())
    {
        // 从空闲链表中删掉之前的span，更新链表头
        Span* span = it->second;
        if(span->next){
            freeSpans_[it->first] = span->next;
        }
        else{//如果下一个没有（分完了）就把这个span从map里面删掉
            freeSpans_.erase(it);
        }

        if(span->numPages > numPages){//如果span大于需要的页数，就切分
            //用一个新的Span先存着切分后的span
            Span* newSpan = new Span;
            newSpan->pageAddr = static_cast<char*>(span->pageAddr) + numPages * PAGE_SIZE;
            newSpan->numPages = span->numPages - numPages;
            newSpan->next = nullptr;

            //切分后的找到对应位置插入进去（头插法）
            auto& list = freeSpans_[newSpan->numPages];
            newSpan->next = list;
            list = newSpan;

            span->numPages = numPages;
        }
        //记录这次分配的span信息，便于后面的回收
        spanMap_[span->pageAddr] = span;
        return span->pageAddr;
    }

    //没找到说明没有，得去要一个来
    void* memory = systemAlloc(numPages);
    if (!memory) return nullptr;

    // 创建新的span
    Span* span = new Span;
    span->pageAddr = memory;
    span->numPages = numPages;
    span->next = nullptr;

     // 记录span信息
    spanMap_[memory] = span;
    return memory;
}

// 释放span 
void PageCache::deallocateSpan(void* ptr, size_t numPages){
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = spanMap_.find(ptr);
    if(it == spanMap_.end()){
        return;
    }
    Span* span = it->second;
    //从ptr的地址开始，去向后找一个span，尝试看能不能从两个小的合并成一个大的span
    void* nextaddr = static_cast<char*>(ptr) + numPages * PAGE_SIZE;
    auto nextIt = spanMap_.find(nextaddr);
    if(nextIt != spanMap_.end()){//如果找到下一个span
        Span* nextSpan = nextIt->second;
        //尝试合并，就得先找到nextSpan在空闲链表中的位置
        bool found = false;
        auto& nextList = freeSpans_[nextSpan->numPages];

        if(nextList == nextSpan)//下一个地址是空闲链表中的头节点
        {
            nextList = nextSpan->next;
            found = true;
        }
        else if(nextList){//不是头节点，需要遍历整个链表
            Span* prev = nextList;//临时变量，用来遍历链表
            while(prev->next){
                if (prev->next == nextSpan)
                {   
                    // 将nextSpan从空闲链表中移除
                    prev->next = nextSpan->next;
                    found = true;
                    break;
                }
                prev = prev->next;//用临时变量prev来遍历链表，找到nextSpan的前一个节点
            }

        }
        
        if(found){
            //合并span
            span->numPages += nextSpan->numPages;
            spanMap_.erase(nextaddr);
            delete nextSpan;
        }
    }
    auto& list = freeSpans_[span->numPages];
    span->next = list;
    list = span;
}
void* PageCache::systemAlloc(size_t numPages)
{
    size_t size = numPages * PAGE_SIZE;

    // 使用mmap分配内存
    void* ptr = mmap(nullptr, size, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (ptr == MAP_FAILED) return nullptr;

    // 清零内存
    memset(ptr, 0, size);
    return ptr;
}
}//namespace Bnxy_memoryPool

