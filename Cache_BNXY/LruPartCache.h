#include "AruNode.h"
#include <unordered_map>

template<typename Key, typename Value>
class LruPartCache 
{
public:
    explicit LruPartCache(size_t capacity, size_t transformThreshold)
        : mainCapacity_(capacity)
        , ghostCapacity_(capacity)
        , transformThreshold_(transformThreshold)
    {
        Initialize();
    }

  
    bool put(Key key, Value value)
    {
        if(mainCapacity_ == 0) return;
        auto it = mainCache_.find(key);
        if(it != mainCache_.end()){
            
            return updateNode(it->second, value);
        }
        return addNewNode(key, value);
        
    }
    bool get(Key key, Value& value, bool& shouldTransform)
    {  
        auto it = mainCache_.find(key);
        if (it != mainCache_.end()) 
        {
            shouldTransform = updateNodeAccess(it->second);
            value = it->second->getValue();
            return true;
        }
        return false;
        
    } 
    bool checkGhostCaches(Key key)
    {
        auto it = ghostCache_.find(key);
        if(it != ghostCache_.end()){
            removeFromGhost(it->second);
            ghostCache_.erase(it);
            return true;
        }
        return false;
    }
    void increaseCapacity() { ++mainCapacity_; }
    
    bool decreaseCapacity() 
    {
        if (mainCapacity_ <= 0) return false;
        if (mainCache_.size() == mainCapacity_) {
            evictLeastRecent();
        }
        --mainCapacity_;
        return true;
    }
private:
  void Initialize()
    {
        mainHead_ = std::make_shared<ArcNode<Key, Value>>();
        mainTail_ = std::make_shared<ArcNode<Key, Value>>();
        mainTail_->prev_ = mainHead_;
        mainHead_->next_ = mainTail_; 

        ghostHead_ = std::make_shared<ArcNode<Key, Value>>();//虚拟头
        ghostTail_ = std::make_shared<ArcNode<Key, Value>>();//虚拟尾
        ghostHead_->next_ = ghostTail_;
        ghostTail_->prev_ = ghostHead_;
    }
    void updateNode(ArcNode<Key, Value>* node, Value value)
    {
        if(mainCapacity_ == 0) return;
        node->setValue(value);
        moveToFront(node);
        
    }
    bool addNewNode(Key key, Value value)
    {

        if(mainCache_.size() >= capacity_){
            evictLeastRecent();
        }
        mainCache_.addNewNode(key, value);
        return true;
    }
     void moveToFront(ArcNode<Key, Value>* node) 
    {
        // 先从当前位置移除
        if (!node->prev_.expired() && node->next_) {
            auto prev = node->prev_.lock();
            prev->next_ = node->next_;
            node->next_->prev_ = node->prev_;
            node->next_ = nullptr; // 清空指针，防止悬垂引用
        }
        
        // 添加到头部
        addToFront(node);
    }
     void evictLeastRecent() 
    {
        ArcNode<Key, Value>* leastRecent = mainTail_->prev_;
        if (!leastRecent || leastRecent == mainHead_) 
            return;

        // 从主链表中移除
        removeFromMain(leastRecent);

        // 添加到幽灵缓存
        if (ghostCache_.size() >= ghostCapacity_) 
        {
            removeOldestGhost();
        }
        addToGhost(leastRecent);

        // 从主缓存映射中移除
        mainCache_.erase(leastRecent->getKey());
    }
    void removeOldestGhost() 
    {
        ArcNode<Key, Value>* oldestGhost = ghostTail_->prev_.lock();
        if (!oldestGhost || oldestGhost == ghostHead_) 
            return;

        // 从幽灵链表中移除
        removeFromGhost(oldestGhost);

        // 从幽灵缓存映射中移除
        ghostCache_.erase(oldestGhost->getKey());
    }
    void removeFromMain(ArcNode<Key, Value>* node) 
    {
        if (!node->prev_.expired() && node->next_) {
            auto prev = node->prev_.lock();
            prev->next_ = node->next_;
            node->next_->prev_ = node->prev_;
            node->next_ = nullptr; // 清空指针，防止悬垂引用
        }
    }

    void removeFromGhost(LruNode<Key, Value>* node) 
    {
        if (!node->prev_.expired() && node->next_) {
            auto prev = node->prev_.lock();
            prev->next_ = node->next_;
            node->next_->prev_ = node->prev_;
            node->next_ = nullptr; // 清空指针，防止悬垂引用
        }
        
        // 添加到头部
        addToFront(node);
    }

    void addToFront(NodePtr node) 
    {
        node->next_ = mainHead_->next_;
        node->prev_ = mainHead_;
        mainHead_->next_->prev_ = node;
        mainHead_->next_ = node;
    }
    
    std::unordered_map<Key, ArcNode<Key, Value>*> mainCache_;
    std::unordered_map<Key, ArcNode<Key, Value>*> ghostCache_;

    std::shared_ptr<ArcNode<Key, Value>> mainHead_;
    std::shared_ptr<ArcNode<Key, Value>> mainTail_;

    std::shared_ptr<ArcNode<Key, Value>> ghostHead_;
    std::shared_ptr<ArcNode<Key, Value>> ghostTail_;

    size_t mainCapacity_;
    size_t ghostCapacity_;
    size_t transformThreshold_;
};