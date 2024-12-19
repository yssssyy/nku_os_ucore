#include <cow.h>
#include <kmalloc.h>
#include <string.h>
#include <sync.h>
#include <pmm.h>
#include <error.h>
#include <sched.h>
#include <elf.h>
#include <vmm.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include<proc.h>

// // 设置页目录——一样√
// static int
// setup_pgdir(struct mm_struct *mm) {
//     struct Page *page;
//     // 分配一个物理页来存储页目录
//     if ((page = alloc_page()) == NULL) {
//         return -E_NO_MEM; // 内存分配失败返回错误码
//     }
//     pde_t *pgdir = page2kva(page); // 获取页目录的虚拟地址
//     memcpy(pgdir, boot_pgdir, PGSIZE); // 复制内核页目录到新页目录

//     mm->pgdir = pgdir; // 将页目录地址存储到内存管理结构中
//     return 0;
// }

// // 释放页目录——一样√
// static void
// put_pgdir(struct mm_struct *mm) {
//     free_page(kva2page(mm->pgdir)); // 释放页目录的物理页
// }

// 拷贝进程的内存管理结构 (实现写时复制)——主要在于内存映射关系的拷贝
int cow_copy_mm(struct proc_struct *proc) {
    struct mm_struct *mm, *oldmm = current->mm;

    // 如果当前进程没有内存管理结构，直接返回
    if (oldmm == NULL) {
        return 0;
    }
    int ret = 0;
    //否则，说明不是内核进程，需要创建内存管理结构
    if ((mm = mm_create()) == NULL) {
        goto bad_mm; // 内存管理结构创建失败
    }
    //分配页目录表并初始化
    if (setup_pgdir(mm) != 0) {
        goto bad_pgdir_cleanup_mm; // 页目录设置失败
    }
    //从父进程拷贝过来内存映射关系
    lock_mm(oldmm); // 加锁保护旧内存结构
    {
        ret = cow_copy_mmap(mm, oldmm); // 拷贝内存映射区域，
    }
    unlock_mm(oldmm); // 解锁

    if (ret != 0) {
        goto bad_dup_cleanup_mmap; // 拷贝失败，清理
    }

    // 设置新进程的内存结构
good_mm:
    mm_count_inc(mm); // 引用计数增加——跟踪每个物理页面的使用情况，内存释放的安全性
    proc->mm = mm;
    proc->cr3 = PADDR(mm->pgdir); // 更新CR3寄存器
    return 0;

bad_dup_cleanup_mmap:
    exit_mmap(mm); // 退出内存映射
    put_pgdir(mm); // 释放页目录
bad_pgdir_cleanup_mm:
    mm_destroy(mm); // 销毁内存管理结构
bad_mm:
    return ret; // 返回错误码
}

// 拷贝内存映射区域 (写时复制)，也进行了内存页的拷贝——share=1设置为共享,也就是不独立，重点在于copy_range
int cow_copy_mmap(struct mm_struct *to, struct mm_struct *from) {
    // 确保目标进程和源进程的内存映射结构体不为 NULL
    assert(to != NULL && from != NULL);

    // 获取源进程内存映射列表的头部，list_entry_t 为链表节点类型
    list_entry_t *list = &(from->mmap_list), *le = list;

    // 遍历源进程的内存映射列表
    while ((le = list_prev(le)) != list) {
        struct vma_struct *vma, *nvma;

        // 从当前链表节点获取虚拟内存区域（VMA）结构体
        vma = le2vma(le, list_link);

        // 为目标进程创建一个新的虚拟内存区域，复制源进程的内存映射区域的起始、结束地址和标志
        nvma = vma_create(vma->vm_start, vma->vm_end, vma->vm_flags);

        // 如果创建失败，返回内存不足的错误码
        if (nvma == NULL) {
            return -E_NO_MEM;
        }

        // 将新的 VMA 插入到目标进程的内存映射结构中
        insert_vma_struct(to, nvma);

        // 使用写时复制机制来拷贝内存区域
        // 调用 cow_copy_range 来处理内存的复制，这里没有直接复制内存内容，而是通过写时复制（COW）来延迟内存的实际拷贝
        // 写时复制意味着在实际写入内存之前，内存区域会被共享，只有在写操作发生时才会进行复制
        if (cow_copy_range(to->pgdir, from->pgdir, vma->vm_start, vma->vm_end) != 0) {
            return -E_NO_MEM; // 拷贝内存失败，返回内存不足的错误码
        }
    }

    // 如果所有操作成功，返回 0
    return 0;
}


// 拷贝页表范围 (写时复制实现)——移除写权限，不进行真正的物理页面的复制
int cow_copy_range(pde_t *to, pde_t *from, uintptr_t start, uintptr_t end) {
    // 确保传入的 start 和 end 地址是页面对齐的，即地址必须是 PGSIZE 的倍数
    assert(start % PGSIZE == 0 && end % PGSIZE == 0);

    // 确保给定的内存范围在用户空间地址范围内，防止非法内存访问
    assert(USER_ACCESS(start, end));

    // 按照页面单位遍历从 start 到 end 之间的每个页面
    do {
        // 获取源进程的页表项 ptep，表示起始地址 start 所对应的物理页
        pte_t *ptep = get_pte(from, start, 0);

        // 如果源进程没有对应的页表项（即该页不存在映射），则跳过此页
        if (ptep == NULL) {
            // 将 start 地址对齐到下一个页的起始地址
            start = ROUNDDOWN(start + PTSIZE, PTSIZE); // 跳过空页表
            continue;
        }

        // 如果源页表项有效（即该页面存在映射）
        if (*ptep & PTE_V) {
            // 清除写权限，表示当前页是共享的，不能直接写
            *ptep &= ~PTE_W;

            // 更新权限，保留用户权限，但移除写权限
            uint32_t perm = (*ptep & PTE_USER & ~PTE_W);

            // 获取源页面的物理页面对象，表示该虚拟页面映射的物理内存,    只复制栈和虚拟内存的页表，不为其分配新的页
            struct Page *page = pte2page(*ptep);
            assert(page != NULL); // 确保物理页面存在

            // 将源页面映射到目标进程的页表中（插入目标进程的页表）
            int ret = page_insert(to, page, start, perm);
            assert(ret == 0); // 确保插入操作成功
        }

        // 移动到下一个页面
        start += PGSIZE;
    } while (start != 0 && start < end); // 当处理的地址未超过结束地址时继续

    return 0; // 返回 0 表示成功
}


// 处理页错误 (写时复制的页面错误处理)——do_pgfault当页表项有效但不可写时调用
// 写时复制页故障处理——为原页面添加写权限PTE_W，实现物理页面的
int cow_pgfault(struct mm_struct *mm, uint_t error_code, uintptr_t addr) {
    // 打印页故障信息，表示发生了写时复制的页错误
    cprintf("COW page fault at 0x%x\n", addr);
    
    int ret = 0;

    // 获取页表项 ptep，表示 addr 所在页面的页表项
    pte_t *ptep = get_pte(mm->pgdir, addr, 0);

    // 获取原页面的权限（用户权限）并添加写权限（PTE_W）
    uint32_t perm = (*ptep & PTE_USER) | PTE_W;

    // 获取原页面的物理页结构（page），表示 addr 所映射的物理内存页
    struct Page *page = pte2page(*ptep);

    // 分配一个新的物理页（npage），以便将原页面的数据复制到新页面
    struct Page *npage = alloc_page();

    // 确保原页面和新页面都成功分配
    assert(page != NULL);
    assert(npage != NULL);

    // 获取原物理页的内核虚拟地址（src），以便进行数据复制
    uintptr_t* src = page2kva(page);
    // 获取新物理页的内核虚拟地址（dst），以便将数据复制到新页
    uintptr_t* dst = page2kva(npage);

    // 复制原页面内容到新页面
    memcpy(dst, src, PGSIZE);//操作系统通过虚拟地址来管理和访问内存，而不是直接操作物理地址。
    //memcpy 是一个内核级的内存复制操作，它处理的是虚拟内存（内核虚拟地址），并且它依赖于内存的分页机制来访问页面内容。

    // 计算页的起始地址，确保页表项和虚拟地址对齐
    uintptr_t start = ROUNDDOWN(addr, PGSIZE);

    // 清除原来页表项，准备插入新的页表项
    *ptep = 0;

    // 在页表中插入新的物理页面映射，使用新的物理页面（npage）并设置权限
    ret = page_insert(mm->pgdir, npage, start, perm);

    // 返回插入操作的结果（通常为 0 表示成功）
    return ret;
}
