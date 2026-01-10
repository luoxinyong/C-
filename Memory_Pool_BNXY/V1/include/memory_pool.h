#pragma once
#include <iostream>
#include <mutex>
#include <thread>
#define MEMORY_POOL_NUM 64
#define SLOT_BASE_SIZE 8
#define MAX_SLOT_SIZE 512

//每一个内存池的槽大小是8的倍数，最大是512字节，这里不直接指定槽的大小
struct slot
{
    slot* next;
};
//内存池类,存放在哈希表中的一个单元,每个内存池管理一个大小为Blocksize的内存块固定为4096，只是slotSize不一样
class Memory_Pool{
public: 
    Memory_Pool(size_t Blocksize = 4096);
    ~Memory_Pool();
    void init(size_t slot_size);
    void* allocate();//申请一个槽，有优先级顺序
    void deallocate(void*);//释放一个槽

private:
    void allocateBlock();//申请一个新的内存块
    size_t alignSize(char* p,size_t size);//内存对齐函数，计算从当前指针p开始，向后需要偏移多少字节可以满足size字节对齐
    
    size_t Blocksize;
    int Slot_Size;
    slot* Free_List;//从已使用后被释放的槽
    slot* First_Block;//指向第一个内存块
    slot* NoUsed_List;//目前没被使用的槽
    slot* Last_Slot;//最后一个槽,超过这个位置再申请就要重新申请新的内存
    std::mutex Mutex_freelist;

};

class HashBucket{
public:
    static void initMemoryPool();
    static void* useMemory(size_t size){
        if(size <= 0)
        {
            return nullptr;
        }
        if(size > MAX_SLOT_SIZE)
        {
            return operator new (size);
        }

        return getMemoryPool(((size+7)/SLOT_BASE_SIZE)-1).allocate();
    }
    static void freememory(void* ptr ,size_t size){
        if(!ptr)
        {
            return;
        }if(size > MAX_SLOT_SIZE)
        {
            operator delete(ptr);
            return;
        }
        getMemoryPool(((size+7)/SLOT_BASE_SIZE)-1).deallocate(ptr);
    }
    static Memory_Pool& HashBucket::getMemoryPool(int index);
    template<typename T, typename... Args>
    friend T* newElement(Args&&... args);

    template<typename T>
    friend void deleteElement(T* p);
};