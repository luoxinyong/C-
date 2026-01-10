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
        //从PageCache申请内存块
        void* GetFromPageCache(size_t size ,size_t& numPages);
        std::array<std::atomic<void*>, FREE_LIST_SIZE> centralFreeList_;//中心缓存的自由链表，保存从Page申请到的内存
        std::array<std::atomic_flag, FREE_LIST_SIZE> locks_;//// 用于同步的自旋锁
        std::array<SpanTracker, 1024> spanTrackers_;//用于判断回收
};

}// namespace Bnxy_memoryPool