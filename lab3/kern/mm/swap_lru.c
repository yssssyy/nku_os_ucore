#include <defs.h>
#include <riscv.h>
#include <stdio.h>
#include <string.h>
#include <swap.h>
#include <swap_lru.h>
#include <list.h>

extern list_entry_t pra_list_head;//声明了一个外部的链表头 pra_list_head，用于管理页面链表。
static int _lru_check(struct mm_struct *mm);

static int _lru_init_mm(struct mm_struct *mm)
{
    list_init(&pra_list_head);    //初始化 pra_list_head 链表
    mm->sm_priv = &pra_list_head; //将 pra_list_head 赋值给 mm->sm_priv，即将进程 mm 的 sm_priv 设置为链表头指针。
    //这是为了后续能够访问与进程相关的页面链表

    cprintf(" mm->sm_priv %x in fifo_init_mm\n", mm->sm_priv);
    return 0;
}

//页面是否可交换
static int _lru_map_swappable(struct mm_struct *mm, uintptr_t addr, struct Page *page, int swap_in)
{
    _lru_check(mm);//检查所有页面的访问情况并更新 visited 值,
    //具体来说，它遍历所有页面链表条目，检查每个页面的访问位（PTE_A），并根据访问情况调整页面的 visited 值。

    list_entry_t *entry = &(page->pra_page_link);//获取页面链表条目：获取页面 page 中的 pra_page_link，
    //这是页面的链表条目（list_entry_t）。它用于将页面链接到链表中

    //确保 entry 不为空。如果为空，则程序会在此处停止并报错。这确保页面的链表条目已正确初始化。
    assert(entry != NULL);
    //从 mm（进程的内存描述符）中的 sm_priv 成员获取链表头指针。
    //sm_priv 是存储与页面管理相关的私有数据，这里是存储页面链表头指针（pra_list_head）
    list_entry_t *head = (list_entry_t *)mm->sm_priv;

    list_add(head, entry); // 将页面page插入到页面链表pra_list_head的末尾
    page->visited = 0;     //标记为未访问
    //将页面的 visited 字段设置为 0，表示页面是新加入链表的，尚未被访问。visited 字段用于LRU算法判断页面的访问历史。
    return 0;
}

static int _lru_swap_out_victim(struct mm_struct *mm, struct Page **ptr_page, int in_tick)
{//从页面链表中选择一个页面进行换出，即选择一个需要被替换的页面
    _lru_check(mm);//检查并更新页面的访问情况。它会遍历链表中的每个页面，检查它们的访问状态，
    //并更新 visited 值。如果页面被访问过，则将 visited 设置为 0；如果没有被访问过，则递增 visited 值。

    list_entry_t *head = (list_entry_t *)mm->sm_priv;//从 mm（进程的内存描述符）中的 sm_priv 成员获取链表头指针。
    assert(head != NULL);
    assert(in_tick == 0);//此断言确保当前函数没有在定时器触发的时间点被调用。
    
    list_entry_t *entry = list_prev(head);//初始化 entry 为链表头的前一个元素（即链表的最后一个元素）
    list_entry_t *pTobeDel = entry;//并将 pTobeDel 设置为 entry。同时，获取 entry 所指向页面的 visited 值，并将其存储在 largest_visted 变量中
    uint_t largest_visted = le2page(entry, pra_page_link)->visited;     //最长时间未被访问的page，比较的是visited
    while (1)
    {
        //entry转一圈，遍历结束
        // 遍历找到最大的visited，表示最早被访问的
        if (entry == head)
        {
            break;
        }
        if (le2page(entry, pra_page_link)->visited > largest_visted)
        //如果该页面的 visited 值大于 largest_visted，表示它是访问时间最久的页面，
    //更新 largest_visted 和 pTobeDel 指向该页面。
        {
            largest_visted = le2page(entry, pra_page_link)->visited;
            // le2page(entry, pra_page_link)->visited = 0;
            pTobeDel = entry;
            // curr_ptr = entry;
        }
        //将 entry 更新为链表中的前一个元素（即继续向链表的前面遍历）。
        entry = list_prev(entry);
    }
    //通过 list_del(pTobeDel) 将 pTobeDel 从链表中删除，表示该页面将被换出
    list_del(pTobeDel);
    //将 pTobeDel 转换为对应的 Page 结构体指针，并通过 ptr_page 返回给调用者，表示被选中的换出页面
    *ptr_page = le2page(pTobeDel, pra_page_link);
    cprintf("curr_ptr %p\n", pTobeDel);
    return 0;
}

static int _lru_check(struct mm_struct *mm)
{
    cprintf("\nbegin check----------------------------------\n");
    list_entry_t *head = (list_entry_t *)mm->sm_priv;   //头指针
    assert(head != NULL);
    list_entry_t *entry = head;
    while ((entry = list_prev(entry)) != head)
    {
        //通过宏 le2page(entry, pra_page_link) 获取 entry 对应的页面结构体指针。pra_page_link 是 Page 结构体中用于链表操作的指针。
        struct Page *entry_page = le2page(entry, pra_page_link);
        //获取页面的页表项：通过 get_pte 函数获取当前页面（entry_page）对应虚拟地址（entry_page->pra_vaddr）的页表项。pgdir 是当前进程的页表目录
        pte_t *tmp_pte = get_pte(mm->pgdir, entry_page->pra_vaddr, 0);
        cprintf("the ppn value of the pte of the vaddress is: 0x%x  \n", (*tmp_pte) >> 10);
        if (*tmp_pte & PTE_A)  //如果近期被访问过，visited清零(visited越大表示越长时间没被访问)
        {
            entry_page->visited = 0;
            *tmp_pte = *tmp_pte ^ PTE_A;//清除访问位
        }
        else
        {
            //未被访问就加一
            entry_page->visited++;
        }

        cprintf("the visited goes to %d\n", entry_page->visited);
    }
    cprintf("end check------------------------------------\n\n");
}

static int _lru_check_swap(void)
{
    // 模拟对多个内存地址的访问
    *(unsigned char *)0x3000 = 0x0c;
    assert(pgfault_num == 4);  // 第一次访问，pgfault_num应该增加到4

    *(unsigned char *)0x1000 = 0x0a;
    assert(pgfault_num == 4);  // 第二次访问，pgfault_num应该仍然是4

    *(unsigned char *)0x4000 = 0x0d;
    assert(pgfault_num == 4);  // 第三次访问，pgfault_num应该仍然是4

    *(unsigned char *)0x2000 = 0x0b;
    assert(pgfault_num == 4);  // 第四次访问，pgfault_num应该仍然是4

    *(unsigned char *)0x5000 = 0x0e;
    assert(pgfault_num == 5);  // 第五次访问，pgfault_num应该增加到5

    *(unsigned char *)0x2000 = 0x0b;
    assert(pgfault_num == 5);  // 重复访问一个页面，pgfault_num应该保持5

    *(unsigned char *)0x1000 = 0x0a;
    assert(pgfault_num == 5);  // 再次访问已经在内存中的页面，pgfault_num应该保持5

    *(unsigned char *)0x2000 = 0x0b;
    assert(pgfault_num == 5);  // 重复访问一个页面，pgfault_num应该保持5

    *(unsigned char *)0x3000 = 0x0c;
    assert(pgfault_num == 5);  // 再次访问已经在内存中的页面，pgfault_num应该保持5

    *(unsigned char *)0x4000 = 0x0d;
    assert(pgfault_num == 5);  // 再次访问已经在内存中的页面，pgfault_num应该保持5

    *(unsigned char *)0x5000 = 0x0e;
    assert(pgfault_num == 5);  // 再次访问已经在内存中的页面，pgfault_num应该保持5

    // 验证页面的值是否正确
    assert(*(unsigned char *)0x1000 == 0x0a);  // 确保 0x1000 地址的数据正确
    *(unsigned char *)0x1000 = 0x0a;  // 修改 0x1000 地址的数据
    assert(pgfault_num == 6);  // 修改时，pgfault_num应该增加到6，表示页面被置换

    return 0;
}

static int _lru_init(void)
{
    return 0;
}

static int _lru_set_unswappable(struct mm_struct *mm, uintptr_t addr)
{
    return 0;
}

static int _lru_tick_event(struct mm_struct *mm)
{
    return 0;
}

struct swap_manager swap_manager_lru =
    {
        .name = "lru swap manager",
        .init = &_lru_init,
        .init_mm = &_lru_init_mm,
        .tick_event = &_lru_tick_event,
        .map_swappable = &_lru_map_swappable,
        .set_unswappable = &_lru_set_unswappable,
        .swap_out_victim = &_lru_swap_out_victim,
        .check_swap = &_lru_check_swap,
};