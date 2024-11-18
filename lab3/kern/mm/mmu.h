#ifndef __KERN_MM_MMU_H__
#define __KERN_MM_MMU_H__

#ifndef __ASSEMBLER__
#include <defs.h>
#endif /* !__ASSEMBLER__ */

// A linear address 'la' has a four-part structure as follows:
//
// +--------9-------+-------9--------+-------9--------+---------12----------+
// | Page Directory | Page Directory |   Page Table   | Offset within Page  |
// |     Index 1    |    Index 2     |                |                     |
// +----------------+----------------+----------------+---------------------+
//  \-- PDX1(la) --/ \-- PDX0(la) --/ \--- PTX(la) --/ \---- PGOFF(la) ----/
//  \-------------------PPN(la)----------------------/
//
// The PDX1, PDX0, PTX, PGOFF, and PPN macros decompose linear addresses as shown.
// To construct a linear address la from PDX(la), PTX(la), and PGOFF(la),
// use PGADDR(PDX(la), PTX(la), PGOFF(la)).

/*
这段注释描述了一个 RISC-V 的线性地址（虚拟地址）结构。地址被分为四部分：

    Page Directory Index 1（PDX1）：高 9 位，表示页目录的第一级索引。
    Page Directory Index 0（PDX0）：接下来 9 位，表示页目录的第二级索引。
    Page Table Index（PTX）：接下来 9 位，表示页表索引。
    Offset within Page（PGOFF）：低 12 位，表示页内偏移。

最终的线性地址由 PDX1, PDX0, PTX 和 PGOFF 组合而成。
*/

// RISC-V uses 39-bit virtual address to access 56-bit physical address!
// Sv39 virtual address:
// +----9----+----9---+----9---+---12--+
// |  VPN[2] | VPN[1] | VPN[0] | PGOFF |
// +---------+----+---+--------+-------+
//
// Sv39 physical address:
// +----26---+----9---+----9---+---12--+
// |  PPN[2] | PPN[1] | PPN[0] | PGOFF |
// +---------+----+---+--------+-------+
//
// Sv39 page table entry:
// +----26---+----9---+----9---+---2----+-------8-------+
// |  PPN[2] | PPN[1] | PPN[0] |Reserved|D|A|G|U|X|W|R|V|
// +---------+----+---+--------+--------+---------------+

/*
PDX1(la) 和 PDX0(la) 宏用于获取给定线性地址 la 的 第一级页目录索引 和 第二级页目录索引。
这通过将地址右移相应的位数并使用位掩码 0x1FF（取出 9 位）实现。
*/
// page directory index
#define PDX1(la) ((((uintptr_t)(la)) >> PDX1SHIFT) & 0x1FF)
#define PDX0(la) ((((uintptr_t)(la)) >> PDX0SHIFT) & 0x1FF)

//PTX(la) 宏用于获取给定线性地址 la 的 页表索引。通过右移地址并使用位掩码 0x1FF 取出 9 位。
// page table index
#define PTX(la) ((((uintptr_t)(la)) >> PTXSHIFT) & 0x1FF)

//PPN(la) 宏获取线性地址 la 对应的 页号（即页表项的物理地址部分），通过右移到页表索引之后的高位。
// page number field of address
#define PPN(la) (((uintptr_t)(la)) >> PTXSHIFT)

//获取给定线性地址 la 在页内的 偏移量，通过与 0xFFF（即 12 位掩码）做按位与操作。
// offset in page
#define PGOFF(la) (((uintptr_t)(la)) & 0xFFF)

//根据提供的页目录索引、页表索引和页内偏移量构造出一个 线性地址。
// construct linear address from indexes and offset
#define PGADDR(d1, d0, t, o) ((uintptr_t)((d1) << PDX1SHIFT | (d0) << PDX0SHIFT | (t) << PTXSHIFT | (o)))

//从页表项（PTE）或页目录项（PDE）中提取出 物理地址 部分,使用 ~0x3FF 清除低 10 位，剩下的高位即为物理地址。
// address in page table or page directory entry
#define PTE_ADDR(pte)   (((uintptr_t)(pte) & ~0x3FF) << (PTXSHIFT - PTE_PPN_SHIFT))
#define PDE_ADDR(pde)   PTE_ADDR(pde)

/* page directory and page table constants */
#define NPDEENTRY       512                    // page directory entries per page directory
#define NPTEENTRY       512                    // page table entries per page table 每个页目录和页表包含的条目数，这里是 512 个。

#define PGSIZE          4096                    // bytes mapped by a page
#define PGSHIFT         12                      // log2(PGSIZE)页面大小的对数值，即 log2(PGSIZE)，用于页内偏移的计算
#define PTSIZE          (PGSIZE * NPTEENTRY)    // bytes mapped by a page directory entry每个页目录项映射的空间大小
#define PTSHIFT         21                      // log2(PTSIZE)用于计算页表项偏移

#define PTXSHIFT        12                      // offset of PTX in a linear address
#define PDX0SHIFT       21                      // offset of PDX0 in a linear address
#define PDX1SHIFT       30                      // offset of PDX0 in a linear address
#define PTE_PPN_SHIFT   10                      // offset of PPN in a physical address

// page table entry (PTE) fields
#define PTE_V     0x001 // Valid
#define PTE_R     0x002 // Read
#define PTE_W     0x004 // Write
#define PTE_X     0x008 // Execute
#define PTE_U     0x010 // User
#define PTE_G     0x020 // Global
#define PTE_A     0x040 // Accessed
#define PTE_D     0x080 // Dirty
#define PTE_SOFT  0x300 // Reserved for Software

#define PAGE_TABLE_DIR (PTE_V)
#define READ_ONLY (PTE_R | PTE_V)
#define READ_WRITE (PTE_R | PTE_W | PTE_V)
#define EXEC_ONLY (PTE_X | PTE_V)
#define READ_EXEC (PTE_R | PTE_X | PTE_V)
#define READ_WRITE_EXEC (PTE_R | PTE_W | PTE_X | PTE_V)

#define PTE_USER (PTE_R | PTE_W | PTE_X | PTE_U | PTE_V)

#endif /* !__KERN_MM_MMU_H__ */

