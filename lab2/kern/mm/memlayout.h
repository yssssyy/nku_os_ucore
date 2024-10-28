#ifndef __KERN_MM_MEMLAYOUT_H__  // 如果没有定义这个宏，则进行下面的定义
#define __KERN_MM_MEMLAYOUT_H__

/* 所有物理内存映射到此地址 */
#define KERNBASE            0xFFFFFFFFC0200000 // 物理内存中内核的起始位置加上偏移量
#define KMEMSIZE            0x7E00000          // 最大物理内存的大小
// 0x7E00000 = 0x8000000 - 0x200000
// QEMU 默认的 RAM 范围为 0x80000000 到 0x88000000, 128MiB, 0x80000000 到 0x80200000 被 OpenSBI 占用
#define KERNTOP             (KERNBASE + KMEMSIZE) // 0x88000000 对应的虚拟地址

#define PHYSICAL_MEMORY_END         0x88000000 // 物理内存的结束地址
#define PHYSICAL_MEMORY_OFFSET      0xFFFFFFFF40000000 // 物理内存偏移量
#define KERNEL_BEGIN_PADDR          0x80200000 // 内核在物理内存中的起始地址
#define KERNEL_BEGIN_VADDR          0xFFFFFFFFC0200000 // 内核在虚拟内存中的起始地址

#define KSTACKPAGE          2                           // 内核栈的页面数量
#define KSTACKSIZE          (KSTACKPAGE * PGSIZE)       // 内核栈的大小

#ifndef __ASSEMBLER__ // 如果不是在汇编代码中

#include <defs.h> // 引入定义相关的头文件
#include <atomic.h> // 引入原子操作相关的头文件
#include <list.h> // 引入链表相关的头文件

typedef uintptr_t pte_t; // 定义页面表项类型为无符号整型
typedef uintptr_t pde_t; // 定义页目录项类型为无符号整型

/* *
 * struct Page - 页面描述结构。每个 Page 描述一个物理页面。
 * 在 kern/mm/pmm.h 中，可以找到许多有用的函数，将 Page 转换为其他数据类型，例如物理地址。
 * */
struct Page {
    int ref;                        // 页面帧的引用计数
    uint64_t flags;                 // 描述页面帧状态的标志数组
    unsigned int property;          // 空闲块的数量，用于首次适配的物理内存管理
    list_entry_t page_link;         // 空闲列表链表
};

/* 描述页面帧状态的标志 */
#define PG_reserved                 0       // 如果该位=1: 页面被保留给内核，不能在 alloc/free_pages 中使用；否则，该位=0 
#define PG_property                 1       // 如果该位=1: 页面是空闲内存块的头页（包含一些连续地址页面），可以用于 alloc_pages；如果该位=0: 如果页面是空闲内存块的头页，则此页面和内存块已分配。或者此页面不是头页。

// 设置页面为保留
#define SetPageReserved(page)       set_bit(PG_reserved, &((page)->flags))
// 清除页面保留标志
#define ClearPageReserved(page)     clear_bit(PG_reserved, &((page)->flags))
// 判断页面是否被保留
#define PageReserved(page)          test_bit(PG_reserved, &((page)->flags))
// 设置页面属性
#define SetPageProperty(page)       set_bit(PG_property, &((page)->flags))
// 清除页面属性
#define ClearPageProperty(page)     clear_bit(PG_property, &((page)->flags))
// 判断页面属性
#define PageProperty(page)          test_bit(PG_property, &((page)->flags))

// 将链表条目转换为页面
#define le2page(le, member)                 \
    to_struct((le), struct Page, member)

/* free_area_t - 维护一个双向链表记录空闲（未使用）页面 */
typedef struct {
    list_entry_t free_list;         // 链表头
    unsigned int nr_free;           // 此空闲列表中空闲页面的数量
} free_area_t;

#endif /* !__ASSEMBLER__ */ // 结束汇编代码判断

#endif /* !__KERN_MM_MEMLAYOUT_H__ */ // 结束宏定义判断
