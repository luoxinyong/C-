/*
 * @Author: bnxy
 * @Date: 2023-04-20 15:20:23
 * @LastEditTime: 2023-04-20 15:20:23
 * @FilePath: \Memory_Pool_BNXY\V2\include\CentralCache.h
 * 1、中心缓存池，在Thread和Page之间的中间层，负责从PageCache获取内存块，然后分配给多个ThreadCache。
 * 
 */
#pragma once
#include <iostream>
#include <array>
#include <thread>
#include "Common.h"
#include "PageCache.h"
#include <unordered_map>

namespace Bnxy_memoryPool
{
    struct BlockHeader_;
    struct SpanTracker;
    static constexpr size_t kNumSizeClasses = FREE_LIST_SIZE;
    //Central元信息存储结构.
struct SpanTracker {
    std::atomic<void*> spanAddr{nullptr};
    std::atomic<size_t> numPages{0};
    std::atomic<size_t> blockSize{0};
    std::atomic<size_t> blockCount{0};
    std::atomic<size_t> freeCount{0}; // 用于追踪spn中还有多少块是空闲的，如果所有块都空闲，则归还span给PageCache
    BlockHeader_* freeList;
    SpanTracker* next;
};
struct BlockHeader_
{
    SpanTracker* span;//指向所属的Span
    BlockHeader_* next;//指向下一个内存块的指针
};


class CentralCache{
    public:
        //获取中心缓存--单例
        static CentralCache* GetInstance(){
            static  CentralCache instance;
            return &instance;
        }
        void* fetchRange(size_t index);//向Thread提供内存块
        void returnRange(void* start, size_t size, size_t index);//将内存块从thread返回给Central缓存



private:


        CentralCache();
        //从PageCache申请内存块
        void* GetFromPageCache(size_t size ,size_t& numPages);
        SpanTracker* getSpanTracker(void* addr);//根据地址获取SpanTracker

        bool shouldPerformDelayedReturn(size_t index, size_t currentCount, std::chrono::steady_clock::time_point currentTime);
        void performDelayedReturn(size_t index);
        void updateSpanFreeCount(SpanTracker* tracker,size_t index);//检查并更新span的空闲块数

        std::array<SpanTracker*, kNumSizeClasses> centralSpanFreeList_;

        std::array<std::atomic<void*>, FREE_LIST_SIZE> centralFreeList_;//中心缓存的自由链表，保存从Page申请到的内存
        std::array<std::atomic_flag, FREE_LIST_SIZE> locks_;//// 用于同步的自旋锁
        std::array<SpanTracker, 1024> spanTrackers_;//用于判断回收
       
        

        
};

}// namespace Bnxy_memoryPool