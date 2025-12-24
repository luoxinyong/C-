#ifndef CACHE_POLICY_H
#define CACHE_POLICY_H


template<typename Key, typename Value>
class CachePolicy
{
public:
    virtual ~CachePolicy() = default;

    virtual void put(Key key, Value value) = 0;
    virtual bool get(Key key, Value& value) = 0;
    // 如果缓存中能找到key，则直接返回value
    virtual Value get(Key key) = 0;

    // virtual void remove(Key key) = 0;
};


#endif