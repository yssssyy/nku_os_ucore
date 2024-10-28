#include <default_pmm.h> // 包含默认物理内存管理器头文件
#include <best_fit_pmm.h> // 包含最佳适配物理内存管理器头文件
#include <slub_pmm.h> // 包含SLUB内存分配器头文件
#include <defs.h> // 包含定义文件
#include <error.h> // 包含错误处理头文件
#include <memlayout.h> // 包含内存布局头文件
#include <mmu.h> // 包含内存管理单元头文件
#include <pmm.h> // 包含物理内存管理头文件
#include <sbi.h> // 包含SBI接口头文件
#include <stdio.h> // 包含标准输入输出头文件
#include <string.h> // 包含字符串处理头文件
#include <../sync/sync.h> // 包含同步头文件
#include <riscv.h> // 包含RISC-V架构相关的头文件

// pages指针保存的是第一个Page结构体所在的位置，也可以认为是Page结构体组成的数组的开头
// 由于C语言的特性，可以把pages作为数组名使用，pages[i]表示顺序排列的第i个结构体
struct Page *pages; // 页面结构体指针，指向页面数组
size_t npage = 0; // 物理内存页面数量
uint64_t va_pa_offset; // 虚拟地址和物理地址的偏移量

// 内存从0x80000000开始，DRAM_BASE在riscv.h中定义为0x80000000
const size_t nbase = DRAM_BASE / PGSIZE; // 基础页面数量

// 虚拟地址的启动时页目录
uintptr_t *satp_virtual = NULL; // 虚拟地址的页目录指针
// 启动时页目录的物理地址
uintptr_t satp_physical; // 物理地址的页目录

// 物理内存管理
const struct pmm_manager *pmm_manager; // 物理内存管理器指针

static void check_alloc_page(void); // 声明检查页面分配的函数

// init_pmm_manager - 初始化物理内存管理器实例
static void init_pmm_manager(void) {
    // pmm_manager = &best_fit_pmm_manager; // 选择最佳适配管理器（注释掉）
    pmm_manager = &slub_pmm_manager; // 选择SLUB内存管理器
    cprintf("memory management: %s\n", pmm_manager->name); // 打印当前内存管理器名称
    pmm_manager->init(); // 调用初始化函数
}

// init_memmap - 调用 pmm->init_memmap 构建空闲内存的 Page 结构
static void init_memmap(struct Page *base, size_t n) {
    pmm_manager->init_memmap(base, n); // 初始化内存映射
}

// alloc_pages - 调用 pmm->alloc_pages 分配连续的 n*PAGESIZE 内存
struct Page *alloc_pages(size_t n) {
    struct Page *page = NULL; // 页面指针初始化为空
    bool intr_flag; // 中断标志
    local_intr_save(intr_flag); // 保存当前中断状态
    {
        page = pmm_manager->alloc_pages(n); // 调用物理内存管理器分配页面
    }
    local_intr_restore(intr_flag); // 恢复中断状态
    return page; // 返回分配的页面
}

// free_pages - 调用 pmm->free_pages 释放连续的 n*PAGESIZE 内存
void free_pages(struct Page *base, size_t n) {
    bool intr_flag; // 中断标志
    local_intr_save(intr_flag); // 保存当前中断状态
    {
        pmm_manager->free_pages(base, n); // 调用物理内存管理器释放页面
    }
    local_intr_restore(intr_flag); // 恢复中断状态
}

// nr_free_pages - 调用 pmm->nr_free_pages 获取当前空闲内存的大小 (nr*PAGESIZE)
size_t nr_free_pages(void) {
    size_t ret; // 返回值
    bool intr_flag; // 中断标志
    local_intr_save(intr_flag); // 保存当前中断状态
    {
        ret = pmm_manager->nr_free_pages(); // 获取空闲页面数量
    }
    local_intr_restore(intr_flag); // 恢复中断状态
    return ret; // 返回空闲页面数量
}

static void page_init(void) {
    va_pa_offset = PHYSICAL_MEMORY_OFFSET; // 硬编码物理内存偏移量

    uint64_t mem_begin = KERNEL_BEGIN_PADDR; // 硬编码内存起始地址
    uint64_t mem_size = PHYSICAL_MEMORY_END - KERNEL_BEGIN_PADDR; // 计算内存大小
    uint64_t mem_end = PHYSICAL_MEMORY_END; // 硬编码内存结束地址

    cprintf("physcial memory map:\n"); // 打印物理内存映射
    cprintf("  memory: 0x%016lx, [0x%016lx, 0x%016lx].\n", mem_size, mem_begin,
            mem_end - 1); // 打印内存大小和范围

    uint64_t maxpa = mem_end; // 最大物理地址

    if (maxpa > KERNTOP) { // 如果最大物理地址超过内核顶端
        maxpa = KERNTOP; // 设置为内核顶端
    }

    extern char end[]; // 外部变量，表示内核结束位置

    npage = maxpa / PGSIZE; // 计算页面数量
    pages = (struct Page *)ROUNDUP((void *)end, PGSIZE); // 将 pages 指针指向内核结束后的第一页

    // 一开始把所有页面都设置为保留给内核使用的，之后再设置哪些页面可以分配给其他程序
    for (size_t i = 0; i < npage - nbase; i++) {
        SetPageReserved(pages + i); // 将页面设置为保留状态
    }

    // 从这个地方开始才是我们可以自由使用的物理内存
    uintptr_t freemem = PADDR((uintptr_t)pages + sizeof(struct Page) * (npage - nbase));
    // 按照页面大小PGSIZE进行对齐
    mem_begin = ROUNDUP(freemem, PGSIZE); // 对齐内存起始地址
    mem_end = ROUNDDOWN(mem_end, PGSIZE); // 对齐内存结束地址
    if (freemem < mem_end) { // 如果可用内存小于结束地址
        // 初始化我们可以自由使用的物理内存
        init_memmap(pa2page(mem_begin), (mem_end - mem_begin) / PGSIZE); // 初始化内存映射
    }
}

/* pmm_init - 初始化物理内存管理 */
void pmm_init(void) {
    // 我们需要分配/释放物理内存（粒度为4KB或其他大小）。
    // 因此在pmm.h中定义了物理内存管理器（struct pmm_manager）的框架。
    // 首先，我们应该基于该框架初始化一个物理内存管理器（pmm）。
    // 然后，pmm可以分配/释放物理内存。
    // 现在可用的有首次适配、最佳适配、最坏适配和伙伴系统的pmm。
    init_pmm_manager(); // 初始化物理内存管理器

    // 检测物理内存空间，保留已经使用的内存，
    // 然后使用 pmm->init_memmap 创建空闲页面列表
    page_init(); // 初始化页面

    // 使用 pmm->check 验证 pmm 中分配/释放函数的正确性
    check_alloc_page(); // 检查页面分配

    extern char boot_page_table_sv39[]; // 外部变量，表示启动时页表
    satp_virtual = (pte_t*)boot_page_table_sv39; // 获取虚拟地址的页表
    satp_physical = PADDR(satp_virtual); // 获取物理地址的页表
    cprintf("satp virtual address: 0x%016lx\nsatp physical address: 0x%016lx\n", satp_virtual, satp_physical); // 打印页表地址
}

static void check_alloc_page(void) {
    pmm_manager->check(); // 调用检查函数
    cprintf("check_alloc_page() succeeded!\n"); // 打印检查成功信息
}
