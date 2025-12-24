/*
    LRU缓存策略<最近最少使用策略>
        1. 缓存容量固定
        2. 当缓存满时，移除最近最少使用的元素
        3. 当缓存中存在元素时，访问该元素，将其移动到最近使用位置
    结构中需要维护
        1. 一个哈希表，用于快速查找缓存中的元素，存储的键值对的键，同时保存在链表中
        2. 一个双向链表，用于维护元素的使用顺序，最近使用的元素在链表头，最少使用的元素在链表尾
        3. 一个变量，用于记录每个元素访问的次数，在更新时加一并更新链表位置
*/
#include <iostream>
#include <unordered_map>
#include <list>
#include "CachePolicy.h"

//提前声明LruCache类，为了在LruNode中访问LruCache的私有成员
template<typename Key, typename Value> class LruCache;
//定义节点
template<typename Key, typename Value>
class LruNode{
public:
        LruNode(Key key, Value value)
            : key_(key), value_(value), prev_(nullptr), next_(nullptr), accescount_(1)
        {}
        const Value& getValue() const { return value_; }
        const Key& getKey() const { return key_; }
        void setValue(Value& value){value_ = value;}
        void incrementAccessCount(){++accescount_;}
        size_t getAccessCount() const{return accescount_;}
private:
        Key key_;
        Value value_;
        ListNode* prev_;
        ListNode* next_;
        size_t accescount_;

    //为了方便，将节点的指针（prev, next）暴露出来进行操作
    friend class LruCache<Key, Value>;
};

template<typename Key, typename Value>
class LruCache : public CachePolicy<Key, Value>{
public:
    LruCache(size_t capacity)
            : capacity_(capacity)
        {
            initializeList();
        }
    
        //更新缓存，如果没有就新增进去
    void put(Key key, Value value) override
        {
            if (capacity_ <= 0)
                return;
            auto it = nodeMap_.find(key);
            if(it != nodeMap_.end()) {
                // 如果在当前容器中,则更新value,并调用get方法，代表该数据刚被访问
                updateNode(it->second, value);
                return ;
            }
            addNewNode(key, value);
        }

    bool get(Key key, Value& value) override
        {
            auto it = nodeMap_.find(key);
            if (it != nodeMap_.end())
            {
                moveToMostRecent(it->second);
                value = it->second->getValue();
                return true;
            }
            return false;
        }
    
    Value get(Key key) override
        {
            Value value{};
            get(key, value);
            return value;
        }
    
    //删除指定元素，包括从链表中删除和从哈希表中删除
    void remove(Key key)
    {
        auto it = nodeMap_.find(key);
        if(it != nodeMap_.end())
        {
            removeNode(it->second);
            nodeMap_.erase(it);
        }
    }

private:
    void initializeList()
            {
                head_ = new ListNode<Key, Value>(Key{}, Value{});//虚拟头
                tail_ = new ListNode<Key, Value>(Key{}, Value{});//虚拟尾
                head_->next_ = tail_;
                tail_->prev_ = head_;
            }
        void updateNode(LruNode<Key, Value>* node, Value value)
        {
            node->setValue(value);
            node->incrementAccessCount();
            moveToMostRecent(node);
        }

    void addNewNode(Key key, Value value)
        {
            if (nodeMap_.size() >= capacity_) {
                // 如果缓存已满，移除最近最少使用的元素
                removeLeastRecent();
            }
            // 创建新节点并添加到链表头
            LruNode<Key, Value>* newNode = new LruNode<Key, Value>(key, value);
            insertNode(newNode);
            nodeMap_[key] = newNode;
            
        }
        //移动节点到最近使用位置（链表头）
    void moveToMostRecent(LruNode<Key, Value>* node)
        {
            // 从当前位置删除节点
            removeNode(node);
            insertNode(node);
        }

    void removeLeastRecent()
        {
            // 移除链表尾的节点（最近最少使用的节点）
            LruNode<Key, Value>* leastRecent = tail_->prev_;
            removeNode(leastRecent);
            // 从哈希表中移除节点
            nodeMap_.erase(leastRecent->key_);
        }
    
    void insertNode(LruNode<Key, Value>* node)
        {
            node->next_ = head_->next_;
            node->prev_ = head_;
            head_->next_->prev_ = node;
            head_->next_ = node;
        }
    // 哈希表，用于快速查找缓存中的元素
    std::unordered_map<Key, LruNode<Key, Value>*> nodeMap_;
    // 双向链表，用于维护元素的使用顺序
    ListNode<Key, Value>* head_;
    ListNode<Key, Value>* tail_;
    // 缓存容量
    size_t capacity_;
};



/*
    LRU缓存策略<最近最少使用策略>
        1. 缓存容量固定
        2. 当缓存满时，移除最近最少使用的元素
        3. 当缓存中存在元素时，访问该元素，将其移动到最近使用位置
    结构中需要维护
        1. 一个哈希表，用于快速查找缓存中的元素，存储的键值对的键，同时保存在链表中
        2. 一个双向链表，用于维护元素的使用顺序，最近使用的元素在链表头，最少使用的元素在链表尾
        3. 一个变量，用于记录每个元素访问的次数，在更新时加一并更新链表位置
*/
