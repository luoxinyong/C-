#include "../include/ThreadCache.h"
#include "../include/CentralCache.h"
#include "../include/Common.h"
namespace Bnxy_memoryPool
{
void* ThreadCache::allocate(size_t size)
{
    //根据size大小来分配内存，分情况讨论
    if(size == 0) //就算等于0，也给他分配一个最小单元的内存块
    {
        size = ALIGNMENT;
    }
    if(size > MAX_BYTES)
    {
        return malloc(size);
    }
    //根据size大小来找到对应的自由链表
    size_t index = SizeClass::getIndex(size);
    _freeListSizes[index]--;
    //如果自由链表为空，就从中心缓存获取
    if(_freeLists[index] == nullptr)
    {
        
        _freeLists[index] = GetfromCentralCache(index);
    }
    //从自由链表中获取内存
     
    void* ret = _freeLists[index];
    
    _freeLists[index] = *reinterpret_cast<void**>(ret);
    
    return ret;
}

void ThreadCache::deallocate(void* ptr, size_t size){
    if (size > MAX_BYTES)
    {
        free(ptr);
        return;
    }

    size_t index = SizeClass::getIndex(size);

    // 插入到线程本地自由链表
    *reinterpret_cast<void**>(ptr) = _freeLists[index];
    _freeLists[index] = ptr;

    // 更新对应自由链表的长度计数
    _freeListSizes[index]++; 

    // 判断是否需要将部分内存回收给中心缓存
    if (shouldReturnToCentralCache(index))
    {
        returnToCentralCache(_freeLists[index], size);
    }
}
void* ThreadCache::GetfromCentralCache(size_t size){
    void* ret = CentralCache::GetInstance()->fetchRange(size);
   
    if(ret == nullptr)
    {
        return nullptr;
    }
    //拿一个内存给线程，别的存进去
    void* result = ret;
    _freeLists[size] = *reinterpret_cast<void**>(ret);

    //更新对应的自由链表长度计数
    size_t num = 0;
    void* current = ret;
    while(current != nullptr)
    {   
        num++;
        current = *reinterpret_cast<void**>(current);
        
    }
    _freeListSizes[size] += num;
    return result;
}
void ThreadCache::returnToCentralCache(void* ptr, size_t size){
    size_t index = SizeClass::getIndex(size);
    size_t alignSize = SizeClass::roundUp(size);

    size_t batchNum = _freeListSizes[index];//计算需要回收的内存块数量
    if(batchNum < 0)
    {
        return;//错误处理
    }
    //计算需要保留的内存块数量和需要回收的内存块数量
    size_t keepNum = std::max(batchNum / 4, size_t(1));
    size_t returnNum = batchNum - keepNum;

    //开始返回
    char* current = reinterpret_cast<char*>(ptr);
    //保留keepNum个内存块
    for(size_t i = 0; i < keepNum; i++)
    {
        current = reinterpret_cast<char*>(*reinterpret_cast<void**>(current));
        if(current == nullptr)
        {
            returnNum = batchNum - (i + 1);
            break;
        }
    }
    //返回returnNum个内存块,这里要把两部分内存之间的联系断开
    if(current != nullptr)
    {
        void* next = *reinterpret_cast<void**>(current);
        *reinterpret_cast<void**>(current) = nullptr;
        //完成断开后，把保留的内存更新到Thread Cache当中
        _freeLists[index] = ptr;
        _freeListSizes[index] = keepNum;
        //其他的返回给中心
        if(returnNum > 0 && next != nullptr)
        {
            CentralCache::GetInstance()->returnRange(next, returnNum*alignSize, index);
        }
    }
    
}


bool ThreadCache::shouldReturnToCentralCache(size_t index){
    //通过检查这个位置的自由链表长度是否超过阈值来判断是否需要将部分内存回收给中心缓存
    size_t threshold = 256;
    return (_freeListSizes[index] > threshold);
}
}//namespace Bnxy_memoryPool

