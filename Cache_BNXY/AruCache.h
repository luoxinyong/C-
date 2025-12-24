#include <unordered_map>
#include "LruCache.h"
#include <mutex>
#include <iostream>

template<typename Key, typename Value>
class AruCache : public CachePolicy<Key, Value>
{
public:
    AruCache(int capacity) : capacity_(capacity), size_(0)
    { }
    ~AruCache(){
        for(auto& pair : nodeMap_){
            delete pair.second;
        }
    }
private:
    int capacity_;
    int partition_;
    LruCache<Key, Value> T1;
    LruCache<Key, Value> T2;
    LruCache<Key, void> B1;
    LruCache<Key, void> B2;


};