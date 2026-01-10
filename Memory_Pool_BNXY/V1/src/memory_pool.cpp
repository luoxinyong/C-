#include "include/memory_pool.h"

Memory_Pool::Memory_Pool(size_t Blocksize) : Blocksize(Blocksize)
{
}
Memory_Pool::~Memory_Pool()
{
    slot* cur = First_Block;
    while(cur){
        slot* next = cur->next;
        
    }
}
void Memory_Pool::init(size_t slot_size)
{
    Slot_Size = slot_size;
    Free_List = nullptr;
    First_Block = nullptr;
    NoUsed_List = nullptr;
    Last_Slot = nullptr;
}
//申请一个槽，有优先级顺序,先从释放的里面找，然后在去找当前的未使用的槽，和最后的比较一下看够不够
void* Memory_Pool::allocate(){
    if(Free_List){
        std::lock_guard<std::mutex> lock(Mutex_freelist);
        if(Free_List){
            slot* ptr = Free_List;
            Free_List = Free_List->next;
            return ptr;
        } 
    }
    slot*tmp;
    if(NoUsed_List>= Last_Slot){
        allocateBlock();
    }
    tmp = NoUsed_List;
    NoUsed_List = NoUsed_List + Slot_Size/sizeof(slot);//因为NoUsed_List是slot类型
    return tmp;
}
void Memory_Pool::deallocate(void* ptr){
    if(ptr){
        std::lock_guard<std::mutex> lock(Mutex_freelist);
        reinterpret_cast<slot*>(ptr)->next = Free_List;
        Free_List = reinterpret_cast<slot*>(ptr);
    }
}//释放一个槽

void Memory_Pool::allocateBlock(){
    //申请新的一块内存块（头插法，需要移动First_Block的位置）
    void* block = operator new (Blocksize);
    reinterpret_cast<slot*>(block)->next = First_Block;
    First_Block = reinterpret_cast<slot*>(block);
    //计算现在未使用的槽的位置
    char* body = reinterpret_cast<char*>(block) + sizeof(slot*);
    size_t paddingSize = alignSize(body, Slot_Size); // 计算对齐需要填充内存的大小
    NoUsed_List = reinterpret_cast<slot*>(body + paddingSize);

    // 超过该标记位置，则说明该内存块已无内存槽可用，需向系统申请新的内存块
    Last_Slot = reinterpret_cast<slot*>(reinterpret_cast<size_t>(block) + Blocksize - Slot_Size);
    Last_Slot = reinterpret_cast<slot*>(block) + Blocksize/sizeof(slot);
    NoUsed_List = First_Block;
}

size_t Memory_Pool::alignSize(char* p, size_t align)
{
    // align 是槽大小
    size_t rem = (reinterpret_cast<size_t>(p) % align);
    return rem == 0 ? 0 : (align - rem);
}



 Memory_Pool& HashBucket::getMemoryPool(int index)
{
    static Memory_Pool memoryPool[MEMORY_POOL_NUM];
    return memoryPool[index];
}

template<typename T, typename... Args>
 T* newElement(Args&&... args){
    T* p = nullptr;
    if((p = reinterpret_cast<T*>(HashBucket::useMemory(sizeof(T)))) != nullptr){
        new(p) T(std::forward<Args>(args)...);
    }
    return p;
 }

template<typename T>
 void deleteElement(T* p){
     if(p){
        p->~T();
        HashBucket::freememory(reinterpret_cast<void*>(p), sizeof(T));
     }
 }