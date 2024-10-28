#include <string.h>
#include <assert.h>
#include <pmm.h>
#include <list.h>
#include <stdio.h>
#include <slub_pmm.h>
//#include <stddef.h> // 确保引入了stddef.h以使用size_t

#define PAGE_SIZE 4096
#define MAX_CACHE_SIZE 2048 // 最大缓存大小
#define CACHE_COUNT 12 // 缓存组数量
#define SLAB_MAX_COUNT 10 // 每个slab的最大数量

// 内存池大小
#define POOL_SIZE (CACHE_COUNT * SLAB_MAX_COUNT * sizeof(Object))

// 对象结构
typedef struct Object {
    struct Object *next;  // 指向下一个对象
} Object;

// slab结构
typedef struct Slab {
    Object *free_list;  // 空闲对象链表
    int in_use;         // 当前使用的对象数量
    struct Slab *next;  // 指向下一个slab
} Slab;

// kmem_cache结构
typedef struct KmemCache {
    Slab *partial;      // 部分使用的slab
    Slab *full;         // 完全使用的slab
    int object_size;    // 每个对象的大小
    Object objects[SLAB_MAX_COUNT]; // 静态数组用于对象
} KmemCache;

// 创建kmem_cache数组
KmemCache kmalloc_caches[CACHE_COUNT];

// 定义内存池
static char memory_pool[POOL_SIZE];
static int pool_index = 0;

// 从内存池中分配内存
void *pool_alloc(size_t size) {
    if (pool_index + size > POOL_SIZE) {
        return NULL; // 内存池不足
    }
    void *ptr = memory_pool + pool_index;
    pool_index += size;
    return ptr;
}

// 释放内存池中的内存（这里只是简单的重置，实际使用中需要管理）
void pool_free() {
    pool_index = 0; // 简单重置内存池
}

// 初始化kmem_cache
void kmem_cache_init() {
    for (int i = 0; i < CACHE_COUNT; i++) {
        if (i < 10) {
            kmalloc_caches[i].object_size = 1 << (i + 3); // 2^3到2^11
        } else if (i == 10) {
            kmalloc_caches[i].object_size = 96; // 96字节
        } else {
            kmalloc_caches[i].object_size = 192; // 192字节
        }
        kmalloc_caches[i].partial = NULL;
        kmalloc_caches[i].full = NULL;

        // 初始化对象链表
        kmalloc_caches[i].partial = pool_alloc(sizeof(Slab)); // 从内存池分配slab
        assert(kmalloc_caches[i].partial != NULL);
        kmalloc_caches[i].partial->free_list = NULL;
        kmalloc_caches[i].partial->in_use = 0;

        for (int j = 0; j < SLAB_MAX_COUNT; j++) {
            Object *obj = &kmalloc_caches[i].objects[j]; // 使用静态数组
            obj->next = kmalloc_caches[i].partial->free_list;
            kmalloc_caches[i].partial->free_list = obj;
        }
    }
    cprintf("kmalloc caches initialized.\n");
}

// 从kmem_cache分配对象
void *slub_alloc(size_t size) {
    for (int i = 0; i < CACHE_COUNT; i++) {
        if (kmalloc_caches[i].object_size == size) {
            KmemCache *cache = &kmalloc_caches[i];
            if (cache->partial) {
                Slab *slab = cache->partial;

                // 从空闲链表中取出一个对象
                if (slab->free_list) {
                    Object *obj = slab->free_list;
                    slab->free_list = obj->next;
                    slab->in_use++;

                    // 如果slab满了，移到full链表
                    if (slab->in_use == SLAB_MAX_COUNT) {
                        slab->next = cache->full; // 移到full链表
                        cache->full = slab;
                        cache->partial = NULL; // 清空partial链表
                    }
                    return obj; // 返回对象指针
                }
            }

            // 如果没有部分slab，则创建一个新的
            Slab *new_slab = (Slab *)pool_alloc(sizeof(Slab)); // 从内存池分配新slab
            assert(new_slab != NULL);
            new_slab->free_list = NULL;
            new_slab->in_use = 0;

            // 初始化对象链表
            for (int j = 0; j < SLAB_MAX_COUNT; j++) {
                Object *obj = &kmalloc_caches[i].objects[j]; // 使用静态数组
                obj->next = new_slab->free_list;
                new_slab->free_list = obj;
            }

            new_slab->next = cache->partial;
            cache->partial = new_slab;

            // 返回第一个空闲对象
            return slub_alloc(size);
        }
    }

    return NULL; // 如果没有匹配的大小，则返回NULL
}

// 释放对象
void slub_free(void *ptr, size_t size) {
    for (int i = 0; i < CACHE_COUNT; i++) {
        if (kmalloc_caches[i].object_size == size) {
            KmemCache *cache = &kmalloc_caches[i];
            Slab *slab = cache->partial;

            // 直接释放到该slab的空闲链表
            if (slab) {
                Object *obj = (Object *)ptr; // 将指针转换为 Object *
                obj->next = slab->free_list;
                slab->free_list = obj;
                slab->in_use--;

                // 如果slab变为空，移除该slab
                if (slab->in_use == 0) {
                    cache->partial = NULL; // 清空partial链表
                }
            }
            return;
        }
    }
}

// 销毁kmem_cache
void destroy_kmem_cache() {
    pool_free(); // 释放内存池
    for (int i = 0; i < CACHE_COUNT; i++) {
        kmalloc_caches[i].partial = NULL; // 清空partial链表
        kmalloc_caches[i].full = NULL;    // 清空full链表
    }
}

// 检查函数
void slub_check() {
    cprintf("SLUB check begin\n");

    for (int i = 0; i < CACHE_COUNT; i++) {
        cprintf("Cache %d: size %d, partial slabs: %p, full slabs: %p\n",
                i, kmalloc_caches[i].object_size, kmalloc_caches[i].partial, kmalloc_caches[i].full);
    }

    for (int i = 0; i < CACHE_COUNT; i++) {
        void *obj = slub_alloc(kmalloc_caches[i].object_size);
        assert(obj != NULL);
        cprintf("Allocated object of size %d: %p\n", kmalloc_caches[i].object_size, obj);
        slub_free(obj, kmalloc_caches[i].object_size);
    }

    destroy_kmem_cache();
    cprintf("kmem_cache destroyed successfully\n");
    cprintf("SLUB check end\n");
}

// 初始化内存映射
void slub_init_memmap() {
    // 这里可以添加内存映射的初始化逻辑
    cprintf("Memory map initialized.\n");
}



// 如果没有 <stddef.h>，你可以这样定义 size_t
//typedef unsigned long size_t; // 请根据实际情况选择合适的类型

/// 返回每个 slab 中的空闲对象数量之和
static size_t slub_nr_free_pages() { // 改为 static size_t
    size_t total_free_objects = 0; // 使用 size_t 类型

    // 遍历每个缓存
    for (int i = 0; i < CACHE_COUNT; i++) {
        KmemCache *cache = &kmalloc_caches[i];
        
        // 遍历部分使用的 slab
        Slab *slab = cache->partial;
        while (slab) {
            // 统计空闲对象的数量
            size_t free_objects_in_slab = 0; // 改为 size_t
            Object *obj = slab->free_list;
            while (obj) {
                free_objects_in_slab++;
                obj = obj->next;
            }
            total_free_objects += free_objects_in_slab;

            // 移动到下一个 slab
            slab = slab->next;
        }

        // 统计完全使用的 slab
        slab = cache->full;
        while (slab) {
            // 完全使用的 slab 中没有空闲对象
            slab = slab->next;
        }
    }

    return total_free_objects; // 返回空闲对象的总数量
}


// 定义 pmm_manager 结构体
const struct pmm_manager slub_pmm_manager = {
    .name = "slub_pmm_manager",
    .init = kmem_cache_init,
    .init_memmap = slub_init_memmap,
    .alloc_pages = (struct Page *(*)(size_t))slub_alloc,   // 类型转换以匹配
    .free_pages = (void (*)(struct Page *, size_t))slub_free, // 类型转换以匹配
    .nr_free_pages = (size_t (*)(void))slub_nr_free_pages,          // 返回可用页面数量
    .check = slub_check,          // 检查函数
};
