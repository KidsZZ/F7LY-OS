#include "slab.hh"
#include "platform.hh"
#include "physical_memory_manager.hh"
#include "printer.hh"

namespace mem {

SlabCache* SlabAllocator::caches[5] = {nullptr};

Slab::Slab(uint32 obj_size) {
    int cnt = 1;
    while (cnt * obj_size <= sizeof(Slab)) cnt++;
    
    pa_start = reinterpret_cast<void*>(reinterpret_cast<uint64>(this) + cnt * sizeof(Slab));
    free_objs_count = (PGSIZE - cnt * sizeof(Slab)) / obj_size;
    max_objs_count = free_objs_count;
    first_obj = reinterpret_cast<uint64>(pa_start);
    is_empty = false;
    
    init_free_objects(obj_size);
}

void Slab::init_free_objects(uint32 obj_size) {
    uint64 current = first_obj;
    uint64 end = reinterpret_cast<uint64>(this) + PGSIZE;
    
    while (current + obj_size < end) {
        uint64 next = current + obj_size;
        *reinterpret_cast<uint64*>(current) = next;
        current = next;
    }
    *reinterpret_cast<uint64*>(current) = 0;
}

void* Slab::alloc() {
    void* obj = reinterpret_cast<void*>(first_obj);
    first_obj = *reinterpret_cast<uint64*>(obj);
    free_objs_count--;
    is_empty = (free_objs_count == 0);
    return obj;
}

void Slab::free(void* obj) {
    *reinterpret_cast<uint64*>(obj) = first_obj;
    first_obj = reinterpret_cast<uint64>(obj);
    free_objs_count++;
    is_empty = (free_objs_count == max_objs_count);
}

SlabCache::SlabCache(uint32 size) : obj_size_(size), free_slabs_count_(0) {
    free_slabs_.clear();
    partial_slabs_.clear();
    full_slabs_.clear();
}

Slab* SlabCache::create_slab() {
    // 分配内存页
    void* page = k_pmm.alloc_page();
    if (!page) return nullptr;
    
    // 手动初始化Slab对象
    Slab* slab = static_cast<Slab*>(page);
    
    // 为metadata预留空间
    int cnt = 1;
    while (cnt * obj_size_ <= sizeof(Slab)) cnt++;
    
    // 初始化链表
    slab->list = LinkedList<Slab*>();
    
    // 设置其他成员
    slab->pa_start = reinterpret_cast<void*>(reinterpret_cast<uint64>(slab) + cnt * sizeof(Slab));
    slab->free_objs_count = (PGSIZE - cnt * sizeof(Slab)) / obj_size_;
    slab->first_obj = reinterpret_cast<uint64>(slab->pa_start);
    slab->max_objs_count = slab->free_objs_count;
    slab->is_empty = false;
    
    // 初始化空闲对象链表
    uint64 current = slab->first_obj;
    uint64 end = reinterpret_cast<uint64>(slab) + PGSIZE;
    
    while (current + obj_size_ < end) {
        uint64 next = current + obj_size_;
        *reinterpret_cast<uint64*>(current) = next;
        current = next;
    }
    *reinterpret_cast<uint64*>(current) = 0;
    
    return slab;
}

void SlabCache::destroy_slab(Slab* slab) {
    slab->~Slab();
    k_pmm.free_page(slab);
}

void* SlabCache::alloc() {
    if (!partial_slabs_.empty()) {
        auto& slab = partial_slabs_.front();
        void* obj = slab.alloc();
        
        if (slab.free_objs_count == 0) {
            partial_slabs_.pop_front();
            full_slabs_.push_front(slab);
        }
        return obj;
    }
    
    if (free_slabs_.empty()) {
        Slab* new_slab = create_slab();
        free_slabs_.push_front(*new_slab);
        free_slabs_count_++;
    }
    
    Slab& slab = free_slabs_.front();
    void* obj = slab.alloc();
    free_slabs_.pop_front();
    partial_slabs_.push_front(slab);
    return obj;
}

void SlabCache::dealloc(void* pa) {
    Slab* slab = reinterpret_cast<Slab*>(PGROUNDDOWN(reinterpret_cast<uint64>(pa)));
    const bool was_full = (slab->free_objs_count == 0);
    
    slab->free(pa);
    
    if (slab->free_objs_count == slab->max_objs_count) {
        // 需要手动查找并移除
        for (auto it = partial_slabs_.begin(); it != partial_slabs_.end(); ++it) {
            if (&(*it) == slab) {
                partial_slabs_.erase(it);
                free_slabs_.push_front(*slab);
                free_slabs_count_++;
                break;
            }
        }
    } else if (was_full) {
        // 需要手动查找并移除
        for (auto it = full_slabs_.begin(); it != full_slabs_.end(); ++it) {
            if (&(*it) == slab) {
                full_slabs_.erase(it);
                partial_slabs_.push_front(*slab);
                break;
            }
        }
    }
    
    memory_recycle();
}

void SlabCache::memory_recycle() {
    while (free_slabs_count_ > DEFAULT_MAX_FREE_SLABS_ALLOWED) {
        Slab& slab = free_slabs_.front();
        free_slabs_.pop_front();
        destroy_slab(&slab);
        free_slabs_count_--;
    }
}

void SlabAllocator::init() {
    caches[0] = new SlabCache(16);
    caches[1] = new SlabCache(32);
    caches[2] = new SlabCache(64);
    caches[3] = new SlabCache(128);
    caches[4] = new SlabCache(256);
    printf("DEBUG:\ncache 1: %p\ncache 2: %p\ncache 3: %p\ncache 4: %p\ncache 5: %p",&caches[0],&caches[1],&caches[2],&caches[3],&caches[4]);
}

void* SlabAllocator::alloc(size_t size) {
    int index = get_cache_index(size);
    if (index >= 0 && index < 5) {
        return caches[index]->alloc();
    }
    return nullptr;
}

void SlabAllocator::dealloc(void* p, size_t size) {
    int index = get_cache_index(size);
    if (index >= 0 && index < 5) {
        caches[index]->dealloc(p);
    }
}

int SlabAllocator::get_cache_index(uint64 size) {
    if (size <= 16) return 0;
    if (size <= 32) return 1;
    if (size <= 64) return 2;
    if (size <= 128) return 3;
    if (size <= 256) return 4;
    return -1;
}

}