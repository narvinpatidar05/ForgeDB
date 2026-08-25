#pragma once

#include "page.h"
#include "page_manager.h"

#include <cstdint>
#include <list>
#include <string>
#include <unordered_map>

// In-memory cache of hot pages — avoids repeated disk seeks.
// Uses a simple LRU eviction policy with a fixed capacity.
class BufferPool {
public:
    BufferPool(PageManager& page_manager, std::size_t capacity);

    Page& get_page(std::uint32_t page_id);
    Page& new_page();

    void mark_dirty(std::uint32_t page_id);
    void flush_all();

    std::uint64_t hits() const;
    std::uint64_t misses() const;
    std::size_t cached_page_count() const;

private:
    struct CachedPage {
        Page page;
        bool dirty = false;
    };

    void evict_if_needed();
    void touch_lru(std::uint32_t page_id);

    PageManager& page_manager_;
    std::size_t capacity_;

    std::unordered_map<std::uint32_t, CachedPage> cache_;
    std::list<std::uint32_t> lru_;  // front = most recently used

    std::uint64_t hits_ = 0;
    std::uint64_t misses_ = 0;
};
