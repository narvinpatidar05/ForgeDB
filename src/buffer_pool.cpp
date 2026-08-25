#include "buffer_pool.h"

BufferPool::BufferPool(PageManager& page_manager, std::size_t capacity)
    : page_manager_(page_manager), capacity_(capacity) {}

Page& BufferPool::get_page(std::uint32_t page_id) {
    const auto it = cache_.find(page_id);
    if (it != cache_.end()) {
        hits_ += 1;
        touch_lru(page_id);
        return it->second.page;
    }

    misses_ += 1;
    evict_if_needed();

    Page page = page_manager_.read_page(page_id);
    cache_[page_id] = CachedPage{page, false};
    lru_.push_front(page_id);
    return cache_[page_id].page;
}

Page& BufferPool::new_page() {
    const std::uint32_t page_id = page_manager_.allocate_new_page();
    evict_if_needed();

    Page page(page_id);
    cache_[page_id] = CachedPage{page, true};
    lru_.push_front(page_id);
    return cache_[page_id].page;
}

void BufferPool::mark_dirty(std::uint32_t page_id) {
    cache_[page_id].dirty = true;
}

void BufferPool::flush_all() {
    for (auto& [page_id, cached] : cache_) {
        if (cached.dirty) {
            page_manager_.write_page(page_id, cached.page);
            cached.dirty = false;
        }
    }
}

std::uint64_t BufferPool::hits() const {
    return hits_;
}

std::uint64_t BufferPool::misses() const {
    return misses_;
}

std::size_t BufferPool::cached_page_count() const {
    return cache_.size();
}

void BufferPool::evict_if_needed() {
    if (cache_.size() < capacity_) {
        return;
    }

    const std::uint32_t victim_id = lru_.back();
    lru_.pop_back();

    auto it = cache_.find(victim_id);
    if (it != cache_.end()) {
        if (it->second.dirty) {
            page_manager_.write_page(victim_id, it->second.page);
        }
        cache_.erase(it);
    }
}

void BufferPool::touch_lru(std::uint32_t page_id) {
    lru_.remove(page_id);
    lru_.push_front(page_id);
}
