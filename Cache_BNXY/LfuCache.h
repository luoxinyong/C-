/*
    LFU缓存策略<最近最少使用策略>
        1. 缓存容量固定，同时使用两个哈希表（或一个哈希一个数组）加上一个双向链表
            1.1 第一个哈希表，保存的是元素的信息，键为元素的键，值为元素的地址，这个地址同时会被链表使用
            1.2 第二个哈希表/数组，用于保存访问次数，键为访问次数，值为对应这个键的一个双向链表
            1.3 双向链表，用于维护元素的使用顺序，最近使用的元素在链表头，最少使用的元素在链表尾
*/
#include <iostream>
#include <unordered_map>
#include <mutex>
#include "CachePolicy.h"

template<typename Key, typename Value> class LfuCache;

template<typename Key, typename Value>
 struct LfuNode{
        Key key_;
        Value value_;
        int freq_;//访问次数，单个节点的访问次数
        LfuNode* prev_;
        LfuNode* next_;
        LfuNode(Key key, Value value, int freq)
                : key_(key)
                , value_(value)
                , freq_(freq)
                , next_(nullptr), prev_(nullptr){}
        LfuNode():freq_(0), next_(nullptr), prev_(nullptr){}
        };

template<typename Key, typename Value>
class FreqList{
public:
        ~FreqList() {
            delete head_;
            delete tail_;
        }
        FreqList(int freq) :freq(freq){
            head_ = new LfuNode();
            tail_ = new LfuNode();
            head_->next_ = tail_; 
            tail_->prev_ = head_;
        }
        int getFreq() const{return freq;}
        bool empty() const{return head_->next_ == tail_;}
        LfuNode* popBack()
        {
            if (empty()) return nullptr;
            LfuNode* node = tail_->prev_;
            removeNode(node);
            return node;
        }
        void insertNode(LfuNode* node)
        {
            node->next_ = head_->next_;
            node->prev_ = head_;
            head_->next_->prev_ = node;
            head_->next_ = node;
        }
        void removeNode(LfuNode* node)
        {
            node->prev_->next_ = node->next_;
            node->next_->prev_ = node->prev_;
            node->next_ = nullptr;
            node->prev_ = nullptr;
        }
private:
        
    LfuNode* head_;
    LfuNode* tail_;
    int freq;//访问次数，该链表的
    friend class LfuCache<Key, Value>;
};


template<typename Key, typename Value>
class LfuCache 
{
    public:
        LfuCache(int capacity) : capacity_(capacity), size_(0), minFreq_(0)
        { }
        ~LfuCache(){
            for(auto& pair : nodeMap_){
                delete pair.second;
            }
            for(auto& pair : freqMap_){
                delete pair.second;
            }
        }
        size_t size() const { return size_; }
        size_t capacity() const { return capacity_; }
        /**
         * @brief 向缓存中插入一个键值对
         * 
         * @param key 键
         * @param value 值
         * 要注意的是整个函数当中的逻辑，每一个情况都需要在内部处理后直接返回，不然容易出现问题
         * 情况一：缓存容量为0，直接返回
         * 情况二：缓存已满且该元素不存在需要插入，则需要删除最少使用的元素
         * 情况三：缓存中不存在该元素，需要插入（不用考虑缓存）
         * 情况四：缓存中存在该元素，需要更新值并增加访问次数（需要从一个链表中删掉该节点，插入到访问次数+1的链表中）
         * 整个过程中要注意minFreq_的更新
         */
        void put(Key key, Value  value){
            std::lock_guard<std::mutex> lock(mtx_);
            //情况一
            if (capacity_ == 0) return;
            //情况二&&情况三，先考虑是不是新的元素，再考虑要不要删除一个元素新增
            
            if(nodeMap_.find(key) != nodeMap_.end()){
                LfuNode* node = nodeMap_[key];

                freqMap_[node->freq_]->removeNode(node);
                node->freq_++;
                if(freqMap_[minFreq_]->head_->next_ == freqMap_[minFreq_]->tail_){
                    minFreq_++;
                }
                if(freqMap_.find(node->freq_) == freqMap_.end()){
                    freqMap_[node->freq_] = new FreqList(node->freq_);
                    
                }
                freqMap_[node->freq_]->insertNode(node);
                node->value_ = value; 
                return;
            }
                if(size_ == capacity_){
                LfuNode* node = freqMap_[minFreq_]->popBack();
                nodeMap_.erase(node->key_);
                delete node;
                size_--;
                }

                LfuNode* node = new LfuNode(key,value,1);
                nodeMap_[key] = node;

                if(freqMap_.find(1) == freqMap_.end()){
                    freqMap_[1] = new FreqList(1);
                }

                freqMap_[1]->insertNode(node);
                size_++;

                minFreq_ = 1;
                return;        
        }

        bool get(Key key){
            std::lock_guard<std::mutex> lock(mtx_);
            if(capacity_ == 0) return false;
            if(nodeMap_.find(key) != nodeMap_.end()){
                LfuNode* node = nodeMap_[key];
                freqMap_[node->freq_]->removeNode(node);
                node->freq_++;
                if(freqMap_[minFreq_]->head_->next_ == freqMap_[minFreq_]->tail_){
                    minFreq_++;
                }
                if(freqMap_.find(node->freq_) == freqMap_.end()){
                    freqMap_[node->freq_] = new FreqList(node->freq_);
                }
                freqMap_[node->freq_]->insertNode(node);
                return true;
            }else{
                return false;
            }
        }
        
    private:
        LfuCache(const LfuCache&) = delete;
        LfuCache& operator=(const LfuCache&) = delete;
        std::unordered_map<Key, LfuNode<Key, Value>*> nodeMap_;
        std::unordered_map<int, FreqList<Key, Value>*> freqMap_;
        int capacity_;
        int size_;
        int minFreq_;
        std::mutex mtx_;

};
/*
 * @brief LfuMeanCache类，增加了平均访问频率的计算，整体增加了一个访问的上限值，在每次新的节点接入或者再次访问时
 * 增加总访问次数，每次访问时判断是否超过上限值，超过则需要重新平衡访问次数，将访问次数超过上限值的节点的访问次数减半
*/
template<typename Key, typename Value>
class LfuMeanCache 
{
    public:
        LfuMeanCache(int capacity, int maxFreq_Num = 10) : capacity_(capacity), size_(0), minFreq_(0), maxFreq_Num(maxFreq_Num), totalFreq_(0), averageFreq_(0)
        { }
        ~LfuMeanCache(){
            for(auto& pair : nodeMap_){
                delete pair.second;
            }
            for(auto& pair : freqMap_){
                delete pair.second;
            }
        }
        size_t size() const { return size_; }
        size_t capacity() const { return capacity_; }
        /**
         * @brief 向缓存中插入一个键值对
         * 
         * @param key 键
         * @param value 值
         * 要注意的是整个函数当中的逻辑，每一个情况都需要在内部处理后直接返回，不然容易出现问题
         * 情况一：缓存容量为0，直接返回
         * 情况二：缓存已满且该元素不存在需要插入，则需要删除最少使用的元素
         * 情况三：缓存中不存在该元素，需要插入（不用考虑缓存）
         * 情况四：缓存中存在该元素，需要更新值并增加访问次数（需要从一个链表中删掉该节点，插入到访问次数+1的链表中）
         * 整个过程中要注意minFreq_的更新
         */
        void put(Key key, Value  value){
            std::lock_guard<std::mutex> lock(mtx_);
            //情况一
            if (capacity_ == 0) return;
            //情况二&&情况三，先考虑是不是新的元素，再考虑要不要删除一个元素新增
            
            if(nodeMap_.find(key) != nodeMap_.end()){
                LfuNode* node = nodeMap_[key];

                freqMap_[node->freq_]->removeNode(node);
                node->freq_++;
                if(freqMap_[minFreq_]->head_->next_ == freqMap_[minFreq_]->tail_){
                    minFreq_++;
                }
                if(freqMap_.find(node->freq_) == freqMap_.end()){
                    freqMap_[node->freq_] = new FreqList(node->freq_);
                    
                }
                freqMap_[node->freq_]->insertNode(node);
                node->value_ = value; 
                addFreqNum(); // 增加平均访问的频率
                return;
            }
                if(size_ == capacity_){
                LfuNode* node = freqMap_[minFreq_]->popBack();
                nodeMap_.erase(node->key_);
                delete node;
                size_--;
                decreaseFreqNum(minFreq_); // 减少平均访问的频率
                }

                LfuNode* node = new LfuNode(key,value,1);
                nodeMap_[key] = node;

                if(freqMap_.find(1) == freqMap_.end()){
                    freqMap_[1] = new FreqList(1);
                }

                freqMap_[1]->insertNode(node);
                addFreqNum(); // 增加平均访问的频率
                size_++;

                minFreq_ = 1;
                return;        
        }

        bool get(Key key){
            std::lock_guard<std::mutex> lock(mtx_);
            if(capacity_ == 0) return false;
            if(nodeMap_.find(key) != nodeMap_.end()){
                LfuNode* node = nodeMap_[key];
                freqMap_[node->freq_]->removeNode(node);
                node->freq_++;
                addFreqNum(); // 增加平均访问的频率
                if(freqMap_[minFreq_]->head_->next_ == freqMap_[minFreq_]->tail_){
                    minFreq_++;
                }
                if(freqMap_.find(node->freq_) == freqMap_.end()){
                    freqMap_[node->freq_] = new FreqList(node->freq_);
                }
                freqMap_[node->freq_]->insertNode(node);
                return true;
            }else{
                return false;
            }
        }
        
    private:
        LfuMeanCache(const LfuMeanCache&) = delete;
        LfuMeanCache& operator=(const LfuMeanCache&) = delete;
        
        void addFreqNum(); // 增加平均访问的频率
        void decreaseFreqNum(int num); // 减少平均访问的频率
        void handleOverMaxAverageNum(); // 处理当前平均访问频率超过上限的情况
        void updateMinFreq();

        std::unordered_map<Key, LfuNode<Key, Value>*> nodeMap_;
        std::unordered_map<int, FreqList<Key, Value>*> freqMap_;
        int capacity_;
        int size_;
        int minFreq_;
        int maxFreq_Num; //平均访问次数的上限值
        int averageFreq_;//平均访问次数
        int totalFreq_;//总访问次数
        std::mutex mtx_;

};
template<typename Key, typename Value>
void LfuMeanCache<Key, Value>::addFreqNum(){
    totalFreq_++;
     if (nodeMap_.empty())
        averageFreq_ = 0;
    else
    averageFreq_ = totalFreq_ / size_;
    if(averageFreq_ > maxFreq_Num){
        handleOverMaxAverageNum();
    }
}
template<typename Key, typename Value>
void LfuMeanCache<Key, Value>::decreaseFreqNum(int num){
    totalFreq_ -= num;

    averageFreq_ = totalFreq_ / size_;
}
template<typename Key, typename Value>
void LfuMeanCache<Key, Value>::handleOverMaxAverageNum(){
    // 处理当前平均访问频率超过上限的情况
    if(nodeMap_.empty()){
        return;
    }
    for(auto it = nodeMap_.begin(); it != nodeMap_.end(); it++){
        LfuNode<Key, Value>* node = it->second;
        if(node->freq_ > maxFreq_Num){
            freqMap_[node->freq_]->removeNode(node);
            node->freq_-= maxFreq_Num / 2;
            if(freqMap_.find(node->freq_) == freqMap_.end()){
                freqMap_[node->freq_] = new FreqList(node->freq_);
            }
            freqMap_[node->freq_]->insertNode(node);
        }
    }
    updateMinFreq();
}

template<typename Key, typename Value>
void LfuMeanCache<Key, Value>::updateMinFreq() 
{
    minFreq_ = INT8_MAX;
    for (const auto& pair : freqMap_) 
    {
        if (pair.second && !pair.second->isEmpty()) 
        {
            minFreq_ = std::min(minFreq_, pair.first);
        }
    }
    if (minFreq_ == INT8_MAX) 
        minFreq_ = 1;
}