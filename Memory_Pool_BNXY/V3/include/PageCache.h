/*
 * @Author: bnxy
 * @Date: 2023-04-20 15:20:23
 * @LastEditTime: 2023-04-20 15:20:23
 * @FilePath: \Memory_Pool_BNXY\V2\include\PageCache.h
 * 1、页缓存池，负责从系统获取内存页，然后分配给多个CentralCache。
 * 经典 best-fit（近似）策略：
 * 需要 3 页
 * 优先拿 3 页
 * 没有就拿 4 页
 * 再没有就拿 8 页
 * 注意的是，如果拿到的比需要的大，就得把这个大小切开，剩下的放到该放的地方
 */
#pragma once
#include <iostream>
#include <array>
#include <thread>
#include <cstring>
#include <mutex>
#include "Common.h"
#include <map>

namespace Bnxy_memoryPool
{
class PageCache{
    public:
     static const size_t PAGE_SIZE = 4096; // 4K页大小
        static PageCache& getInstance()
        {
            static PageCache instance;
            return instance;
        }
     // 分配指定页数的span
        void* allocateSpan(size_t numPages);

        // 释放span 
        void deallocateSpan(void* ptr, size_t numPages);

    private:
        struct Span
        {
            void*  pageAddr; // 页起始地址
            size_t numPages; // 页数
            Span*  next;     // 链表指针
        };
        PageCache() = default;
        void* systemAlloc(size_t numPages); 
        std::mutex mutex_;
        // 按页数管理空闲span，不同页数对应不同Span链表
        std::map<size_t, Span*> freeSpans_;
        // 页号到span的映射，用于回收
        std::map<void*, Span*> spanMap_;

};
}// namespace Bnxy_memoryPool