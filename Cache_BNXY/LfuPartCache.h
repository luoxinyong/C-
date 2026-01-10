#include "LfuCache.h"
template<typename Key, typename Value>
class LfuPartCache 
{
public:

    explicit LfuPartCache(size_t capacity, size_t transformThreshold)
        : LfuCache<Key, Value>(capacity, transformThreshold)
    {
    }

private:
    LfuCache<Key, Value> mainCache_;
    LfuCache<Key, Value> ghostCache_;
    int capacity_;
    int transformThreshold_;
};
