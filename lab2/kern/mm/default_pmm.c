#include <pmm.h>          // 引入物理内存管理相关的头文件
#include <list.h>        // 引入链表操作相关的头文件
#include <string.h>      // 引入字符串操作相关的头文件
#include <default_pmm.h> // 引入默认物理内存管理相关的头文件

/* First-Fit Memory Allocator (FFMA) 说明:
   FFMA算法通过维护一个空闲块链表（free list），在接收到内存请求时，扫描链表找到第一个足够大的块。
   如果找到的块比请求的块大得多，通常会将其拆分，剩余部分重新添加到链表中。
   参考文献：Yan Wei Min的《数据结构 -- C程序设计语言》第196~198页，第8.2节。 */

// 你需要重写以下函数：default_init、default_init_memmap、default_alloc_pages、default_free_pages。

/*
 * FFMA的细节
 * (1) 准备：为实现First-Fit Mem Alloc (FFMA)，我们需要使用一些链表管理空闲内存块。
 *              free_area_t结构用于管理空闲内存块。首先你应该熟悉list.h中的struct list，这是一个简单的双向链表实现。
 *              你需要知道如何使用：list_init、list_add（list_add_after）、list_add_before、list_del、list_next、list_prev
 *              另一个技巧是将一般链表结构转换为特殊结构（如struct page）：
 *              你可以找到一些宏：le2page（在memlayout.h中），（在以后的实验中：le2vma（在vmm.h中），le2proc（在proc.h中）等）
 * (2) default_init：你可以重用示例default_init函数来初始化free_list并将nr_free设置为0。
 *              free_list用于记录空闲内存块。nr_free是空闲内存块的总数。
 * (3) default_init_memmap：调用图：kern_init --> pmm_init-->page_init-->init_memmap--> pmm_manager->init_memmap
 *              此函数用于初始化一个空闲块（参数：addr_base, page_number）。
 *              首先，你应该初始化这个空闲块中的每个页面（在memlayout.h中），包括：
 *                  p->flags应设置为PG_property（表示此页面是有效的。在pmm_init函数中（在pmm.c中），
 *                  p->flags中设置了PG_reserved位）。
 *                  如果此页面是空闲的且不是空闲块的第一个页面，则p->property应设置为0。
 *                  如果此页面是空闲的且是空闲块的第一个页面，则p->property应设置为块的总数。
 *                  p->ref应设置为0，因为p是空闲的且没有引用。
 *                  我们可以使用p->page_link将此页面链接到free_list，例如：list_add_before(&free_list, &(p->page_link)); 
 *              最后，我们应该统计空闲内存块的数量：nr_free+=n
 * (4) default_alloc_pages：在空闲列表中搜索找到第一个空闲块（块大小 >=n）并调整空闲块的大小，返回malloced块的地址。
 *              (4.1) 所以你应该这样搜索free list：
 *                       list_entry_t le = &free_list;
 *                       while((le=list_next(le)) != &free_list) {
 *                       .....
 *                 (4.1.1) 在while循环中，获取struct page并检查p->property（记录空闲块的数量）>=n?
 *                       struct Page *p = le2page(le, page_link);
 *                       if(p->property >= n){ ...
 *                 (4.1.2) 如果我们找到这个p，说明我们找到了一个空闲块（块大小 >=n），
 *                     第一个n页可以被分配。一些页面的标志位应被设置：PG_reserved =1，PG_property =0
 *                     从free_list中unlink页面
 *                     (4.1.2.1) 如果(p->property >n)，我们应重新计算剩余空闲块的数量，
 *                           (例如：le2page(le,page_link))->property = p->property - n;）
 *                 (4.1.3) 重新计算nr_free（所有空闲块的剩余数量）
 *                 (4.1.4) 返回p
 *               (4.2) 如果我们找不到空闲块（块大小 >=n），则返回NULL
 * (5) default_free_pages：将页面重新链接到free list，可能合并小的空闲块为大的空闲块。
 *               (5.1) 根据撤回块的基地址，在空闲列表中搜索，找到正确的位置（从低到高地址），并插入页面。
 *                     （可能使用list_next，le2page，list_add_before）
 *               (5.2) 重置页面的字段，例如p->ref，p->flags（PageProperty）
 *               (5.3) 尝试合并低地址或高地址块。注意：应正确更改一些页面的p->property。
 */

// 定义一个free_area结构体以管理空闲内存块
free_area_t free_area;

#define free_list (free_area.free_list) // 定义free_list为free_area中的free_list
#define nr_free (free_area.nr_free)       // 定义nr_free为free_area中的nr_free

// 初始化空闲内存管理
static void default_init(void) {
    list_init(&free_list); // 初始化空闲列表
    nr_free = 0;           // 初始化空闲内存块计数为0
}

// 初始化内存映射
static void default_init_memmap(struct Page *base, size_t n) {
    assert(n > 0); // 确保请求的页数大于0
    struct Page *p = base;
    for (; p != base + n; p++) {
        assert(PageReserved(p)); // 确保页面是保留状态
        p->flags = p->property = 0; // 清空页面标志和属性
        set_page_ref(p, 0); // 设置页面引用计数为0
    }
    base->property = n; // 设置第一个页面的属性为总页数
    SetPageProperty(base); // 设置该页面为属性页
    nr_free += n; // 更新空闲页数统计
    if (list_empty(&free_list)) { // 如果空闲列表为空
        list_add(&free_list, &(base->page_link)); // 将该页面添加到空闲列表
    } else {
        list_entry_t* le = &free_list; // 初始化链表入口
        while ((le = list_next(le)) != &free_list) { // 遍历链表
            struct Page* page = le2page(le, page_link); // 获取页面
            if (base < page) { // 如果基地址小于当前页面
                list_add_before(le, &(base->page_link)); // 在当前页面前插入
                break; // 退出循环
            } else if (list_next(le) == &free_list) { // 如果已到达链表末尾
                list_add(le, &(base->page_link)); // 在链表末尾插入
            }
        }
    }
}

// 分配页面
static struct Page *default_alloc_pages(size_t n) {
    assert(n > 0); // 确保请求的页数大于0
    if (n > nr_free) { // 如果请求的页数大于可用页数
        return NULL; // 返回NULL
    }
    struct Page *page = NULL; // 定义页面指针
    list_entry_t *le = &free_list; // 初始化链表入口
    while ((le = list_next(le)) != &free_list) { // 遍历空闲列表
        struct Page *p = le2page(le, page_link); // 获取页面
        if (p->property >= n) { // 如果空闲块的属性大于或等于请求的页数
            page = p; // 找到合适的空闲块
            break; // 退出循环
        }
    }
    if (page != NULL) { // 如果找到了合适的页面
        list_entry_t* prev = list_prev(&(page->page_link)); // 获取前一个节点
        list_del(&(page->page_link)); // 从空闲列表中删除该页面
        if (page->property > n) { // 如果剩余部分大于请求的页数
            struct Page *p = page + n; // 定义新的页面指针
            p->property = page->property - n; // 更新新页面的属性
            SetPageProperty(p); // 设置该页面为属性页
            list_add(prev, &(p->page_link)); // 将新页面添加回空闲列表
        }
        nr_free -= n; // 更新空闲页数统计
        ClearPageProperty(page); // 清除页面的属性标志
    }
    return page; // 返回分配的页面
}

// 释放页面


static void
default_free_pages(struct Page *base, size_t n) {
    // 确保要释放的页面数量大于零。
    assert(n > 0);
    
    struct Page *p = base; // 将指针 p 初始化为要释放页面的基地址。
    for (; p != base + n; p ++) { // 遍历要释放的每一页。
        // 确保页面没有被保留，也没有属性标记。
        assert(!PageReserved(p) && !PageProperty(p));
        p->flags = 0; // 重置页面的标志。
        set_page_ref(p, 0); // 将页面的引用计数设为 0，表示页面现在是空闲的。
    }
    
    base->property = n; // 将基页面的属性设置为释放的页面数量。
    SetPageProperty(base); // 为基页面设置属性标志。
    nr_free += n; // 更新空闲页面的总数。

    // 检查空闲列表是否为空。
    if (list_empty(&free_list)) {
        list_add(&free_list, &(base->page_link)); // 将基页面添加到空闲列表。
    } else {
        list_entry_t* le = &free_list; // 初始化列表条目为空闲列表的头部。
        while ((le = list_next(le)) != &free_list) { // 遍历空闲列表。
            struct Page* page = le2page(le, page_link); // 从列表获取当前页面。
            // 根据地址将新的页面块按顺序插入空闲列表中。
            if (base < page) {
                list_add_before(le, &(base->page_link)); // 在找到的页面前插入。
                break;
            } else if (list_next(le) == &free_list) {
                list_add(le, &(base->page_link)); // 添加到列表末尾。
            }
        }
    }

    // 尝试与前一个块合并（如果相邻）。
    list_entry_t* le = list_prev(&(base->page_link)); // 获取前一个列表条目。
    if (le != &free_list) {
        p = le2page(le, page_link); // 从前一个条目获取页面。
        // 检查前一个块是否正好在基页面前面结束。
        if (p + p->property == base) {
            p->property += base->property; // 合并属性计数。
            ClearPageProperty(base); // 清除基页面的属性标志。
            list_del(&(base->page_link)); // 从列表中删除基页面。
            base = p; // 更新基页面为合并后的页面。
        }
    }

    // 尝试与下一个块合并（如果相邻）。
    le = list_next(&(base->page_link)); // 获取下一个列表条目。
    if (le != &free_list) {
        p = le2page(le, page_link); // 从列表获取下一个页面。
        // 检查基页面是否正好在下一个块前面结束。
        if (base + base->property == p) {
            base->property += p->property; // 合并属性计数。
            ClearPageProperty(p); // 清除下一个块的属性标志。
            list_del(&(p->page_link)); // 从列表中删除下一个块。
        }
    }
}

static size_t
default_nr_free_pages(void) {
    return nr_free; // 返回当前空闲页面的总数。
}

static void
basic_check(void) {
    struct Page *p0, *p1, *p2; // 声明三个页面指针。
    p0 = p1 = p2 = NULL; // 初始化为 NULL。

    // 分配三个页面，并确保它们不为 NULL。
    assert((p0 = alloc_page()) != NULL);
    assert((p1 = alloc_page()) != NULL);
    assert((p2 = alloc_page()) != NULL);

    // 确保分配的页面互不相同。
    assert(p0 != p1 && p0 != p2 && p1 != p2);
    // 确保每个页面的引用计数为 0。
    assert(page_ref(p0) == 0 && page_ref(p1) == 0 && page_ref(p2) == 0);

    // 确保每个页面的物理地址是有效的。
    assert(page2pa(p0) < npage * PGSIZE);
    assert(page2pa(p1) < npage * PGSIZE);
    assert(page2pa(p2) < npage * PGSIZE);

    // 存储当前空闲列表的状态并重置。
    list_entry_t free_list_store = free_list;
    list_init(&free_list);
    assert(list_empty(&free_list));

    // 存储当前空闲页面的计数并重置。
    unsigned int nr_free_store = nr_free;
    nr_free = 0;

    // 确保空闲列表为空时无法分配页面。
    assert(alloc_page() == NULL);

    // 释放之前分配的页面。
    free_page(p0);
    free_page(p1);
    free_page(p2);
    assert(nr_free == 3); // 确保现在空闲页面数量为 3。

    // 再次分配页面。
    assert((p0 = alloc_page()) != NULL);
    assert((p1 = alloc_page()) != NULL);
    assert((p2 = alloc_page()) != NULL);
    
    // 确保无法再分配更多页面。
    assert(alloc_page() == NULL);

    // 释放一个页面并检查空闲列表。
    free_page(p0);
    assert(!list_empty(&free_list)); // 确保空闲列表不为空。

    struct Page *p;
    // 分配一个页面并确保是刚释放的那个。
    assert((p = alloc_page()) == p0);
    assert(alloc_page() == NULL); // 确保无法再分配页面。

    assert(nr_free == 0); // 确保没有空闲页面。
    free_list = free_list_store; // 恢复原始空闲列表。
    nr_free = nr_free_store; // 恢复原始空闲计数。

    // 再次释放所有页面。
    free_page(p);
    free_page(p1);
    free_page(p2);
}



// LAB2: 下面的代码用于检查首次适配分配算法
// 注意：您**不应**更改 basic_check 和 default_check 函数！
static void
default_check(void) {
    int count = 0, total = 0; // 初始化计数器和总量
    list_entry_t *le = &free_list; // 从空闲列表的头开始
    // 遍历空闲列表
    while ((le = list_next(le)) != &free_list) {
        struct Page *p = le2page(le, page_link); // 获取当前页面
        assert(PageProperty(p)); // 确保页面有属性标志
        count ++, total += p->property; // 更新计数和总属性
    }
    // 确保总属性与当前空闲页面数量一致
    assert(total == nr_free_pages());

    basic_check(); // 执行基本的分配检查

    struct Page *p0 = alloc_pages(5), *p1, *p2; // 分配 5 个页面
    assert(p0 != NULL); // 确保分配成功
    assert(!PageProperty(p0)); // 确保分配的页面没有属性标志

    // 存储当前空闲列表状态并重置
    list_entry_t free_list_store = free_list;
    list_init(&free_list); // 初始化空闲列表
    assert(list_empty(&free_list)); // 确保空闲列表为空
    assert(alloc_page() == NULL); // 确保无法分配页面

    unsigned int nr_free_store = nr_free; // 存储当前空闲页面计数
    nr_free = 0; // 重置空闲页面计数

    // 释放 p0 的第 3 个页面开始的 3 个页面
    free_pages(p0 + 2, 3);
    assert(alloc_pages(4) == NULL); // 确保无法分配 4 个页面
    assert(PageProperty(p0 + 2) && p0[2].property == 3); // 检查释放页面的属性

    // 分配 3 个页面并确保它们与释放的页面相邻
    assert((p1 = alloc_pages(3)) != NULL);
    assert(alloc_page() == NULL); // 确保无法再分配页面
    assert(p0 + 2 == p1); // 确保它们是连续的

    p2 = p0 + 1; // 将 p2 指向 p0 的前一个页面
    free_page(p0); // 释放 p0
    free_pages(p1, 3); // 释放 p1 的页面
    assert(PageProperty(p0) && p0->property == 1); // 检查 p0 的属性
    assert(PageProperty(p1) && p1->property == 3); // 检查 p1 的属性

    // 分配一个页面并释放，然后重新分配
    assert((p0 = alloc_page()) == p2 - 1); // 应该分配到 p2 前面的页面
    free_page(p0); // 再次释放它
    assert((p0 = alloc_pages(2)) == p2 + 1); // 分配 p2 后面的 2 个页面

    free_pages(p0, 2); // 释放这 2 个页面
    free_page(p2); // 释放 p2

    // 再次分配 5 个页面并确保成功
    assert((p0 = alloc_pages(5)) != NULL);
    assert(alloc_page() == NULL); // 确保无法再分配页面

    assert(nr_free == 0); // 确保空闲页面计数为 0
    nr_free = nr_free_store; // 恢复原始空闲页面计数

    free_list = free_list_store; // 恢复原始空闲列表
    free_pages(p0, 5); // 释放之前分配的 5 个页面

    le = &free_list; // 重新遍历空闲列表
    // 遍历空闲列表，确保所有页面的属性被正确减少
    while ((le = list_next(le)) != &free_list) {
        struct Page *p = le2page(le, page_link); // 获取当前页面
        count --, total -= p->property; // 更新计数和总属性
    }
    assert(count == 0); // 确保计数归零
    assert(total == 0); // 确保总属性归零
}

// 这个结构体用于管理页面内存
const struct pmm_manager default_pmm_manager = {
    .name = "default_pmm_manager", // 管理器名称
    .init = default_init, // 初始化函数
    .init_memmap = default_init_memmap, // 初始化内存映射函数
    .alloc_pages = default_alloc_pages, // 分配页面函数
    .free_pages = default_free_pages, // 释放页面函数
    .nr_free_pages = default_nr_free_pages, // 获取空闲页面数量函数
    .check = default_check, // 检查函数
};
