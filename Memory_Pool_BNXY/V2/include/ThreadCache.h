/*
 * @Author: bnxy
 * @Date: 2023-04-20 15:20:23
 * @LastEditTime: 2023-04-20 15:20:23
 * @FilePath: \Memory_Pool_BNXY\V2\include\ThreadCache.h
 * 1、提供一个获取线程缓存的接口，每个在该进程当中的线程都需要通过这个类【ThreadCache】来获取缓存单例。
 * 2、每个线程缓存当中维护一个自由链表数组和统计数组，自由链表数组的大小为128KB，每个自由链表当中存储的是大小为2^n的内存块。
 */
#pragma once
#include <iostream>
#include <array>
#include "Common.h"
namespace Bnxy_memoryPool
{
class ThreadCache
{
public:
    //获取线程缓存--单例
    static ThreadCache* GetInstance(){
        static thread_local ThreadCache instance;
        return &instance;
    }
    //申请内存
    void* allocate(size_t size);
    //释放内存
    void deallocate(void* ptr, size_t size);


private:
    ThreadCache() 
    {
        // 初始化自由链表和大小统计
        _freeLists.fill(nullptr);
        _freeListSizes.fill(0);
    }
    //从上层获取内存
    void* GetfromCentralCache(size_t size);
    //释放内存给中心
    void returnToCentralCache(void* ptr, size_t size);
    bool shouldReturnToCentralCache(size_t index);
private:
    std::array<void*, FREE_LIST_SIZE> _freeLists;//该线程自由链表数组
    std::array<size_t, FREE_LIST_SIZE> _freeListSizes;
};

}