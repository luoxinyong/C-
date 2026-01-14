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
#include <unordered_map>

namespace Bnxy_memoryPool
{
    //Span信息存储结构
struct SpanTracker {
    std::atomic<void*> spanAddr{nullptr};
    std::atomic<size_t> numPages{0};
    std::atomic<size_t> blockCount{0};
    std::atomic<size_t> freeCount{0}; // 用于追踪spn中还有多少块是空闲的，如果所有块都空闲，则归还span给PageCache
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
        void updateSpanFreeCount(SpanTracker* tracker, size_t newFreeBlocks, size_t index);//检查并更新span的空闲块数

        static const size_t MAX_DELAY_COUNT = 48;  // 最大延迟计数
        std::array<std::atomic<size_t>, FREE_LIST_SIZE> delayCounts_;//延迟归还计数，用于判断是否需要延迟归还，每种大小都有一个
        std::array<std::chrono::steady_clock::time_point, FREE_LIST_SIZE> lastReturnTimes_;  // 上次归还时间
        static const std::chrono::milliseconds DELAY_INTERVAL;  // 延迟间隔


        std::array<std::atomic<void*>, FREE_LIST_SIZE> centralFreeList_;//中心缓存的自由链表，保存从Page申请到的内存
        std::array<std::atomic_flag, FREE_LIST_SIZE> locks_;//// 用于同步的自旋锁
        std::array<SpanTracker, 1024> spanTrackers_;//用于判断回收
        std::atomic<size_t> spanCount_{0};//span的计数器，用于给spanTrackers_分配索引
        

        
};

}// namespace Bnxy_memoryPool