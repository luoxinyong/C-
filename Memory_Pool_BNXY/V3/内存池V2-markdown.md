# 内存池V2-markdown

## ThreadCache部分

#### 1、类内对象生成方式解析

```cpp
static ThreadCache* GetInstance(){
        static thread_local ThreadCache instance;
        return &instance;
    }
```

 	这个类需要每个线程调用```GetInstance（）```函数去获取一个线程内存池单例对象，具体的显式执行在最外层的```MemoryPool```当中，这个类是整个内存池提供给用户的公开API接口，用于获取内存，内部的线程级别--中心池级别--page级别都是隐藏的。

​	同时，由于```thread_local```关键字的使用，每个线程都是线程安全的，不会出现变量被多个线程共享的情况。

#### 2、获取单例后对象中数组解析

在线程使用对外API创建了单例对象后，在线程池中会自动生成一个```ThreadCache```对象，初始化时会调用

```cpp
 ThreadCache() 
    {
        // 初始化自由链表和大小统计
        _freeLists.fill(nullptr);
        _freeListSizes.fill(0);
    }
```

来创建两个数组```std::array<void*, FREE_LIST_SIZE> _freeLists;```和```std::array<size_t, FREE_LIST_SIZE> _freeListSizes;```

下面对这两个数组进行解析：

- ```std::array<void*, FREE_LIST_SIZE> _freeLists;```表示的是该线程**当前缓存了哪些大小的空闲内存块**
- ```std::array<size_t, FREE_LIST_SIZE> _freeListSizes;```表示的是该线程**对于每种大小的内存块，分别换成了多少块，用于调度和回收**

以实际例子来具体说明：

##### 第一步：线程T1被创建（这里直接用ThreadCache来说明）

```cpp
ThreadCache* tc = ThreadCache::getInstance();
```

做了什么？实际执行了下面

```cpp
static thread_local ThreadCache instance;
然后调用
ThreadCache() 
	{
        // 初始化自由链表和大小统计
        _freeLists.fill(nullptr);
        _freeListSizes.fill(0);
    }
```

- T1第一次访问
- 创建了一个只属于T1的ThreadCache

###### 此时在线程T1当中：

- 没有任何一个缓存的内存块
- 也没有任何一个freelist当中的节点

##### 第二步：线程T1第一次分配内存：allocate（64）

进入到ThreadCache当中，申请内存，具体的执行过程是：

###### 1、计算这个内存对应的大小

```cpp
static size_t getIndex(size_t bytes)
    {   
        // 确保bytes至少为ALIGNMENT
        bytes = std::max(bytes, ALIGNMENT);
        // 向上取整后-1
        return (bytes + ALIGNMENT - 1) / ALIGNMENT - 1;
    }
bytes = 64
index = ( 64 + 8 -1 ) / 8 -1 = 7（取整）
```

###### 2、查询线程的自由内存链表_freeLists

```cpp
_freeLists[7] == nullptr;
```

这说明在对应index=7的位置，现在还不存在缓存，那么就需要向Central Cache去申请一些内存（大于所需内存量）

###### 3、向Central Cache申请一批内存（假定有10个）

```
Central Cache向Thread Cache分配了10个64B大小的内存块
```

###### 4、Thread Cache更新

```
从10个内存块当中拿出1个给用户使用
剩余9个就放到freelist当中挂起
```

此时的线程T1中数组的状态为

```
_freeLists[7]  ->指向了那剩余9个的64B内存块组成的链表
对应的
_freeListSizes[7] = 9;  ->在大小为64B的位置上还有9个可用的空白内存块可以分配
其余位置的freeLists = nullptr ，freeListSize = 0；
```

##### 第三步：线程T1再去申请一块大小为64的内存：allocate（64）

###### 1、计算这个内存对应的大小

```cpp
void* p2 = allocate(64);

bytes = 64
index = 7
```

###### 2、找到对应的数组位置
```cpp
ptr = _freelists[7];
_freeLists[7] = next(ptr);
_freeListSizes[7]--;
```
此时发生的变化是：
```cpp
_freeLists[7]  -> 指向剩余8个内存块的链表
_freeListSize[7] = 8;
```
没有向系统调用内存申请、只修改了这两个数组完成了这次内存分配。
##### 第三步：线程T1再次申请内存：allocate（128）
###### 1、计算对应大小
```cpp
btyes = 128;
index = ( 128 + 8 -1 )/8 -1 = 15;
```
查找对应数组
```cpp
_freeLists[15] = nullptr;
```
说明对应128B位置现在不存在可用内存块->再次向Central Cache申请一批
###### 2、申请一批内存（10个）
更新对应数组
```cpp
_freeLists[15]  ->指向剩余9个的128B内存块
_freeListSize[15] = 9;
```
##### 第四步 释放内存deallocate（p1）
###### 1、计算对应数组
```cpp
deallocate(p1); //p1内存为64B
index = 7;
```
###### 2、找到对应链表，返回内存
将P1的地址用头插法插入自由内存链表，更新数量

```cpp
//头插法插入
*reinterpret_cast<void**>(ptr) = freeList_[7];
    freeList_[7] = ptr;
    // 更新对应自由链表的长度计数
    freeListSize_[7]++;
```

结束。

#### 3、xx= *reinterpret_cast<void**>(xx);指针移动的解析

​	在Thread Cache的内存分配和释放之中，因为要移动_freeLists[index]的位置来保证它指向的是空闲内存链表的头地址，会经常用到一个代码：

```cpp
void* ret = _freeLists[index];
    _freeLists[index] = *reinterpret_cast<void**>(ret);
```

​	这里的注释就写着，遍历或指向下一个内存块，为什么呢？

​	这里```*reinterpret_cast<void**>(xx)```的效果就等同于**“从空闲链表中取出一个内存块，头指针后移到下一个块”**，具体的机制是**通过读取“内存块内部保存的next指针”实现**

###### 原因：

每一个空闲的内存块都有一个固定的隐式格式：

```text
空闲状态的内存块布局：

+----------------------+
| next (void*)         |  ← 这 8 字节（64 位）
+----------------------+
| 剩余可用空间         |
| ...                  |
+----------------------+

```

**在这个内存块属于“未被使用”的状态时，这个内存块的起始地址保存了一个“next”指针，用于指向下一个空闲内存块的起始地址**

那么回到之前的代码：``````*reinterpret_cast<void**>(xx)``````，他起到了什么作用？

**从xx这个位置开始，读取一个8字节的内容，读取的格式是把这8字节当作一个（void*)来读**，因为

```text
+----------------------+
| next (void*)         |  ← *(void**)xx
+----------------------+
| ...                  |

```

这里**”xx = 当前节点保存的next指针”**，将自己的值进行替换后，自然而然的，xx就指向了新的一块内存的起始地址，在逻辑上就等同于**“向后移动了一个内存块”**。



## CentralCache部分

阅读方式：从和ThreadCache重合的部分开始，函数```void* fetchRange(size_t index);//向Thread提供内存块```以及```void returnRange(void* start, size_t size, size_t index);//将内存块从thread返回给Central缓存```。其余结构体和函数都是为了这两个函数的实现服务的。

#### 1、申请来的内存怎么样分割好，发给Thread一份，其余保存在自己那里

根据```fetchRange（）```的逻辑，会先从自己的自由链表```centralFreeList_[index]```当中尝试获得所需要的内存，如果有，直接分配；如果没有，说明需要去从Page Cache申请。

下面细看函数逻辑：

###### （1）异常处理

###### （2）使用自旋锁去获得自由链表的修改权，使用acquire保证后面所有的读写都必须在获取锁之后

###### （3）开始尝试获取内存给Thread Cache使用：

- 如果自己有，就从自由链表中拿到第一个地址对应的内存块，返回给Thread Cache。同时断开两个之间的链接，更新。

- 如果自己没有，说明需要去Page Cache申请，**需要注意的是，这里从Page Cache申请到的内存是一整块内存，需要根据Thread Cache需要的大小进行分割**

  ---

  分割策略：

  ```cpp
  void* CentralCache::GetFromPageCache(size_t size ,size_t& numPages)
  {   
      // 1. 计算实际需要的页数
      size_t numPages = (size + PageCache::PAGE_SIZE - 1) / PageCache::PAGE_SIZE;
  
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
  }`
  ```

  这里对函数进行了修改，将实际需要的页数直接返回。

  ---

  在获得到实际页数和那一整块内存后，下面要做的就是根据Thread Cache所需的大小进行分割：

  ```cpp
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
  ```
  
  这里要注意的是，在分割申请下来的span时的操作方式，是通过**char***来移动指针的，而不是直接使用**void***，这里涉及到C++以及内存池当中的一些规则和核心：
  
  **void***是一种通用的指针类型，她最关键的点在于**不能直接解引用，也不能进行指针的运算**，因为它只是一个内存的地址，不会关联任何的数据类型信息，所以编译器是无法确定这个内存被怎么样解释的。在这里的操作中，本质的含义是**“在一整块连续的内存中，按照size大小的字节一块一块的向后移动并切块”**，所以使用**void***去执行时不允许的。
  
  **char***在C/C++中被标准规定了：①```sizeof(char) == 1``` ②```char* + n``` = 向后移动n个字节③**char*指针可以用于访问<font color=red>任何</font>其他类型对象的原始内存字节**，这一点在序列化与反序列化当中尤其重要
  
  所以在这里进行类似于移动的操作必须要用**char***。

#### 2、SpanTracker结构体---内存管理机制解析

​	在中心缓存Central Cache中，有一个结构体Span在类内一直穿插生效，贯穿整个获取内存以及回收内存的整体逻辑中，下面通过这个结构体来一窥整个Central Cache的内存管理机制。

###### 结构体SpanTracker

~~~cpp
struct SpanTracker {
    std::atomic<void*> spanAddr{nullptr};
    std::atomic<size_t> numPages{0};
    std::atomic<size_t> blockCount{0};
    std::atomic<size_t> freeCount{0}; 
};
~~~

从名称上可以一眼看出：

- spanAddr：这个span的地址
- numPages：这个span从Page申请下来多少页数
- blockCount：这个span按照Thread所需的size被划分成了多少个内存块的数量
- free Count：这个span当中还有多少空闲的block可以分配（主要用于将内存回收到Page）

然后关注到它的作用，从**1、**可以知道，当一片内存从Central进行分配时，如果他自己没有，他回去向Page申请numpage个内存片，那么一旦某个size大小的内存需求量大，Central可能会一直去向Page申请内存，于是就引出了一个问题：等我要去归还内存的时候，我怎么知道哪些block合并起来是之前某一次从Page申请到的内存？所以需要一个变量/结构体来告诉开发者“这几个block和那几个block合并起来后就是原先申请到的一片内存，可以归还”，其中的缘由就是，**既然从Page申请到的内存是一整块内存，而且是一整块地址连续的内存，那么在归还的时候，也必须按照“一整块”的概念去还，不能是零散的分段小内存。**

​	所以在申请内存函数fetchRange（）中有这么一段：

~~~cpp
size_t trackerIndex = spanCount_++;
if (trackerIndex < spanTrackers_.size())
{
    spanTrackers_[trackerIndex].spanAddr.store(start, std::memory_order_release);
    spanTrackers_[trackerIndex].numPages.store(numPages, std::memory_order_release);
    spanTrackers_[trackerIndex].blockCount.store(blockNum, std::memory_order_release); // 共分配了blockNum个内存块
    spanTrackers_[trackerIndex].freeCount.store(blockNum - 1, std::memory_order_release); // 第一个块result已被分配出去，所以初始空闲块数为blockNum - 1
}
~~~

这里SpanTracker这个结构体就被初次被定义并赋值了，代表的是这次向Page申请到的内存地址是多少，申请了多少页，分割出来的总数以及剩余的空闲数量是多少。后续的使用中，如果是从这里的空闲内存块进行分配，就会同步使用原子变量操作的形式将数量减一。

​	既然在申请的时候开辟了多个结构体，那么这个结构体当然也会在内存回收的使用中起到作用，下面将视角放到内存的回收。关注到函数returnRange（）当中，我们注意到函数中有一步更新计数以及检查归还的操作：

~~~cpp
//2、更新延迟计数
size_t currentCount = delayCounts_[index].fetch_add(1, std::memory_order_relaxed) + 1;
auto currentTime = std::chrono::steady_clock::now();
// 3. 检查是否需要执行延迟归还
if (shouldPerformDelayedReturn(index, currentCount, currentTime))
{
    performDelayedReturn(index);
}
~~~

​	这里讲的是**延迟归还机制**。在内存池的开发中，PageCache→CentralCache→ThreadCache的内存申请是实时的，但如果反过来ThreadCache→CentralCache→PageCache的内存回收也是“立即回收”的，那么会导致高频的malloc/free操作，单个block会在Central和Page之间被来回使用，这反而增加了Page到Central之间内存分配和回收的成本，所以这里CentralCache采用的策略是：

> **优先在 Central 内部“蓄水”，只有“确定一个 span 真的空了”才还给 PageCache**

但面对的问题是：

- CentralCache 管理的是 **block**
- PageCache 只能回收 **完整 span**
- Central 并不知道一个 span 什么时候“全空”

##### （1）延迟归还的触发条件（并不是每次return都会去执行归还）

在```returnRange()```当中已经看到了三步操作：1、把归还的block链表接回到centralFreeList_[index]；2、延迟计数加一 delayCounts_[index]++; 同步记录当前归还时间 3、shouldPerformDelayedReturn（）判断计数上或时间上是否够久。

这说明**触发条件由两个关系组成**

1、数量阈值

- 同一个size的block
- 触发return的次数累积到一定数量

2、时间阈值

- 距离上次执行归还PageCache的时间大于一定时长

本质上就是一句话

> **“Central里面的空闲block足够多/挂的足够久了，需要一次整体的清理”**

##### （2）延迟归还的归还操作---performDelayedReturn()做了什么呢

###### ①遍历整个CentralFreeList_[index]

~~~cpp
void* currentBlock = centralFreeList_[index];
while (currentBlock) {
    SpanTracker* tracker = getSpanTracker(currentBlock);
    spanFreeCounts[tracker]++;
    currentBlock = next;
}
~~~

这样的目的是：在当前这个Central中，这个size class下每一个span里面各有多少个空闲的block可以使用。这里必须要按照span的级别来区分，同时为了提升效率，这里使用了```unordered_map<SpanTracker*，size_t>```来保证后续的搜索速度。

###### ②检查每个Span的空闲数量----updateSpanFreeCount（）

~~~cpp
size_t oldFreeBlocks = tracker->freeCount.load(std::memory_order_relaxed);
size_t currentFreeBlocks = oldFreeBlocks + newFreeBlocks;
tracker->freeCount.store(currentFreeBlocks, std::memory_order_release);

// 如果所有块都空闲，归还span
if (currentFreeBlocks == tracker->blockCount.load(std::memory_order_relaxed))
~~~

这里通过检查历史已知的空闲数量+这次扫描到新增的数量来得到整体的空闲数量。

***

<font color=red>**注意！这里的内存计算方式应该是错误的，在实际测试中会很容易出现freeCount>blockCount的情况**</font>

---

###### ③判断当前这个span里面是不是全是空白内存

略过了，就是两个变量的大小比较一下，唯一的目的就是保证**当一个span的所有block都回到Central之后，再把这个span去还给PageCache**



## PageCache部分

阅读方式和Central Cache类似，从Page和Central的交集开始，所有的函数和结构体都是为了这两个函数服务的。

#### 1、结构体和变量说明

下面来看看结构体和变量，既然Page的职责是从系统申请内存，并向Central分配它所需的内存，那么为了整个内存管理的便利，代码里先不用看来猜一猜有哪些？

- 肯定有一个变量跟之前的自由链表类似的，来管理不同大小的span
- 为了方便分配内存，也肯定跟Central一样有一个结构体来记录每次申请下来的内存的信息：起码要有一个首地址、这片内存的大小和一个指向下一片内存的指针
- 最后为了方便将内存还给系统，在Central归还内存的时候，我起码要知道这次还给我的内存在之前分配的时候它属于哪一个span

现在对照着代码来瞅瞅，这几个东西对应的是什么。

##### （1）结构体Span

```cpp
struct Span
        {
            void*  pageAddr; // 页起始地址
            size_t numPages; // 页数
            Span*  next;     // 链表指针
        };
```

很明显，这个结构体代表就是前面第二部分的猜测，它作为**Page Cache当中的最小管理单元**，是用于描述**”一段连续页内存当前的所有权以及边界“**的对象，它只会**在两种情况下被创建或被修改**:

- **向系统申请新的页**
- **对现有的大/小span进行切割/合并**

那么结构体内部的变量分别是干嘛的也就一目了然了，```void* pageAddr```代表的就是这个span的起始地址，```size_t numPages```代表的就是这个span有多少页数，对应的内存大小=```numPages * PAGE_SIZE ```，```Span* next```指的就是在同一个```numPages```的规格下下一个空闲Span的地址，因为在整个Page当中，每一个规格的span同样是以链表的形式被空闲自由链表管理的。

##### （2）std::map<size_t, Span*> freeSpans_;

这个很自然的能看出，它所对应的就是Thread当中的freeLists，Central当中的centralFreeList，作用就是**按照span大小分类的空闲span集合保存**，唯一的区别就是这里使用的保存方式是有序map，而其它两个是数组。

在这个map当中，保存的key是页数，value是这个页数对应的空闲span链表的首地址。空闲链表的链接就是通过Span结构体当中的next指针链接的，具体的链接就是在归还和分配的时候进行的

##### （3）std::map<void, Span> spanMap_;

这个变量的含义是**一个页的起始地址到Span元数据**的映射，作用是**”从任意页的起始地址“→找到它属于哪个span，以及这个span有多少页**，主要用于PageCache的内存回收。下面具体解析一下：

当Central的内存向上回收的时候，传给Page的是：**（void* ptr,  size_t numPages）**,ptr = span的起始地址， numPages = span对应的页数，那么问题来了：**PageCache怎么样确认这个ptr是不是之前它分配下去的？有怎么确定它原先是几页的？（这里看分配时的解析）**。所以需要一个只凭地址就是找到对应Span的变量。

| 场景 | 查什么      | 能不能       |
| ---- | ----------- | ------------ |
| 分配 | 页数 → span | ✅ freeSpans_ |
| 回收 | 地址 → span | ❌ freeSpans_ |
| 回收 | 地址 → span | ✅ spanMap_   |

---

#### 2、内存管理解析---分配和回收

##### （1）allocateSpan —— PageCache 如何“分配页”

目标：**给我一段”至少numPages页“的连续内存**

###### ①在freeSpans_中查找有没有可复用span

这里就是通过有序哈希map去找到一个**”页数大于需求numPages的最小span“**，这是一个简单但有效的best-fit策略。

###### ②找到了同级别numPages的空闲span → 取出

移动空闲链表头到下一个，把找到的第一个切分，从对应链表中一处

###### ③找到的Span比需求numPages大 → 切割

**切割逻辑**

原 span:  [-----------------------]   (8 页)
需求:        [--------]                     (3 页)
剩余:                   [--------------]   (5 页)

代码对应：

~~~cpp
newSpan->pageAddr = span->pageAddr + numPages * PAGE_SIZE;
newSpan->numPages = span->numPages - numPages;
~~~

然后：

- **剩余部分的span，作为一个新的newSpan从原链表里移除**
- 放到切分后的freeSpans当中

把这里分配下去的内存进行一次记录，记录这个span的信息

###### ④没找到，得去系统申请

<font color=red>**注意！这里的申请方式是按需索取，并不会扩容索要**</font>

所以新申请到的内存就刚好是**numPages页**，不切割，直接把这个新申请的信息作为一个新的Span保存进去

##### （2）deallocateSpan —— PageCache 如何“回收页”

目标：**把一段”连续页的span“归还给PageCache，并尝试和后面的span进行合并，形成新的更大的span**

###### ①用spanMap_来判断回收的内存的合法性

**PageCache只回收自己分配的span**，并且保存回收的**ptr一定是span的起始内存**

###### ②尝试与“后一个 span”合并

~~~cpp
void* nextAddr = ptr + numPages * PAGE_SIZE;
auto nextIt = spanMap_.find(nextAddr);
~~~

逻辑含义：

> 如果 **下一个地址刚好是另一个 span 的起点**
>  那说明这两个 span 在物理内存中是连续的

然后：

- 检查 nextSpan 是否真的在空闲链表中
- 只有 当下一个span是**空闲 span** 才允许合并

**整体合并逻辑**

~~~cpp
span->numPages += nextSpan->numPages;
spanMap_.erase(nextAddr);
delete nextSpan;
~~~

等同的效果=

~~~text
[ span ][ nextSpan ]  →  [    merged span    ]
~~~

最后把这个新的span插入到空闲链表

<font color=red>**注意！这里的合并方式并不合理，只是一个简约的教学版本，只去和后一个span进行检查尝试合并**</font>

为什么不合理呢，举个栗子🌰

假设原始内存布局（物理连续）：

~~~less
A | B | C | D
~~~

###### 情况 1：顺序归还（理想情况）

归还顺序：A → B → C → D

- 归还 A：
  - next = B（已在 spanMap_，但 B 未空闲）→ ❌ 不合并
- 归还 B：
  - next = C（未空闲）→ ❌
- 归还 C：
  - next = D（未空闲）→ ❌
- 归还 D：
  - next = nullptr
  - 但 **前面的 C 没检查** → ❌

👉 **最终：完全没有合并**

---

###### 情况 2：反序归还（恰好触发一次合并）

归还顺序：D → C → B → A

- 归还 D：无合并
- 归还 C：
  - next = D（空闲）→ ✅ 合并成 CD
- 归还 B：
  - next = C（地址是 CD 的起始）→ ✅ 合并成 BCD
- 归还 A：
  - next = B → ✅ 合并成 ABCD

👉 **只有在“反向连续归还”时，才会完全合并**

###### ③就算下一个nextspan并不能合并

那就直接把这个归还的span直接当作一个新的空闲链表头插法插入到整体的链表里

