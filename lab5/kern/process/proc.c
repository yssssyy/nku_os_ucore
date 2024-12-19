#include <proc.h>
#include <kmalloc.h>
#include <string.h>
#include <sync.h>
#include <pmm.h>
#include <error.h>
#include <sched.h>
#include <elf.h>
#include <vmm.h>
#include <trap.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <cow.h>
/* ------------- process/thread mechanism design&implementation -------------
(an simplified Linux process/thread mechanism )
introduction:
  ucore implements a simple process/thread mechanism. process contains the independent memory sapce, at least one threads
for execution, the kernel data(for management), processor state (for context switch), files(in lab6), etc. ucore needs to
manage all these details efficiently. In ucore, a thread is just a special kind of process(share process's memory).
------------------------------
process state       :     meaning               -- reason
    PROC_UNINIT     :   uninitialized           -- alloc_proc
    PROC_SLEEPING   :   sleeping                -- try_free_pages, do_wait, do_sleep
    PROC_RUNNABLE   :   runnable(maybe running) -- proc_init, wakeup_proc, 
    PROC_ZOMBIE     :   almost dead             -- do_exit

-----------------------------
process state changing:
                                            
  alloc_proc                                 RUNNING
      +                                   +--<----<--+
      +                                   + proc_run +
      V                                   +-->---->--+ 
PROC_UNINIT -- proc_init/wakeup_proc --> PROC_RUNNABLE -- try_free_pages/do_wait/do_sleep --> PROC_SLEEPING --
                                           A      +                                                           +
                                           |      +--- do_exit --> PROC_ZOMBIE                                +
                                           +                                                                  + 
                                           -----------------------wakeup_proc----------------------------------
-----------------------------
process relations
parent:           proc->parent  (proc is children)
children:         proc->cptr    (proc is parent)
older sibling:    proc->optr    (proc is younger sibling)
younger sibling:  proc->yptr    (proc is older sibling)
-----------------------------
related syscall for process:
SYS_exit        : process exit,                           -->do_exit
SYS_fork        : create child process, dup mm            -->do_fork-->wakeup_proc
SYS_wait        : wait process                            -->do_wait
SYS_exec        : after fork, process execute a program   -->load a program and refresh the mm
SYS_clone       : create child thread                     -->do_fork-->wakeup_proc
SYS_yield       : process flag itself need resecheduling, -- proc->need_sched=1, then scheduler will rescheule this process
SYS_sleep       : process sleep                           -->do_sleep 
SYS_kill        : kill process                            -->do_kill-->proc->flags |= PF_EXITING
                                                                 -->wakeup_proc-->do_wait-->do_exit   
SYS_getpid      : get the process's pid

*/

// the process set's list
list_entry_t proc_list;

#define HASH_SHIFT          10
#define HASH_LIST_SIZE      (1 << HASH_SHIFT)
#define pid_hashfn(x)       (hash32(x, HASH_SHIFT))

// has list for process set based on pid
static list_entry_t hash_list[HASH_LIST_SIZE];

// idle proc
struct proc_struct *idleproc = NULL;
// init proc
struct proc_struct *initproc = NULL;
// current proc
struct proc_struct *current = NULL;

static int nr_process = 0;

void kernel_thread_entry(void);
void forkrets(struct trapframe *tf);
void switch_to(struct context *from, struct context *to);

// alloc_proc - alloc a proc_struct and init all fields of proc_struct
static struct proc_struct *
alloc_proc(void) {
    struct proc_struct *proc = kmalloc(sizeof(struct proc_struct));
    if (proc != NULL) {
    //LAB4:EXERCISE1 YOUR CODE
    /*
     * below fields in proc_struct need to be initialized
     *       enum proc_state state;                      // Process state
     *       int pid;                                    // Process ID
     *       int runs;                                   // the running times of Proces
     *       uintptr_t kstack;                           // Process kernel stack
     *       volatile bool need_resched;                 // bool value: need to be rescheduled to release CPU?
     *       struct proc_struct *parent;                 // the parent process
     *       struct mm_struct *mm;                       // Process's memory management field
     *       struct context context;                     // Switch here to run process
     *       struct trapframe *tf;                       // Trap frame for current interrupt
     *       uintptr_t cr3;                              // CR3 register: the base addr of Page Directroy Table(PDT)
     *       uint32_t flags;                             // Process flag
     *       char name[PROC_NAME_LEN + 1];               // Process name
     */

     //LAB5 YOUR CODE : (update LAB4 steps)
     /*
     * below fields(add in LAB5) in proc_struct need to be initialized  
     *       uint32_t wait_state;                        // waiting state
     *       struct proc_struct *cptr, *yptr, *optr;     // relations between processes
     */

        proc->state = PROC_UNINIT;//给进程设置为未初始化状态
        proc->pid = -1;//未初始化的进程，其pid为-1
        proc->runs = 0;//初始化时间片,刚刚初始化的进程，运行时间一定为零	
        proc->kstack = 0;//内核栈地址,该进程分配的地址为0，因为还没有执行，也没有被重定位，因为默认地址都是从0开始的。
        proc->need_resched = 0;//不需要调度
        proc->parent = NULL;//父进程为空
        proc->mm = NULL;//虚拟内存为空
        memset(&(proc->context), 0, sizeof(struct context));//初始化上下文
        proc->tf = NULL;//中断帧指针为空
        proc->cr3 = boot_cr3;//页目录为内核页目录表的基址
        proc->flags = 0; //标志位为0
        memset(proc->name, 0, PROC_NAME_LEN+1);//进程名为0

        proc->wait_state = 0;//初始化为 0 表示该进程没有处于等待状态。
        proc->cptr = NULL; // Child Pointer 表示当前进程的子进程
        proc->optr = NULL; // Older Sibling Pointer 表示当前进程的上一个兄弟进程
        proc->yptr = NULL; // Younger Sibling Pointer 表示当前进程的下一个兄弟进程
    }
    return proc;
}

// set_proc_name - set the name of proc
char *
set_proc_name(struct proc_struct *proc, const char *name) {
    memset(proc->name, 0, sizeof(proc->name));
    return memcpy(proc->name, name, PROC_NAME_LEN);
}

// get_proc_name - get the name of proc
char *
get_proc_name(struct proc_struct *proc) {
    static char name[PROC_NAME_LEN + 1];
    memset(name, 0, sizeof(name));
    return memcpy(name, proc->name, PROC_NAME_LEN);
}

// set_links - set the relation links of process
static void
set_links(struct proc_struct *proc) {
    list_add(&proc_list, &(proc->list_link));
    proc->yptr = NULL;
    if ((proc->optr = proc->parent->cptr) != NULL) {
        proc->optr->yptr = proc;
    }
    proc->parent->cptr = proc;
    nr_process ++;
}

// remove_links - clean the relation links of process
static void
remove_links(struct proc_struct *proc) {
    list_del(&(proc->list_link));
    if (proc->optr != NULL) {
        proc->optr->yptr = proc->yptr;
    }
    if (proc->yptr != NULL) {
        proc->yptr->optr = proc->optr;
    }
    else {
       proc->parent->cptr = proc->optr;
    }
    nr_process --;
}

// get_pid - alloc a unique pid for process
static int
get_pid(void) {
    static_assert(MAX_PID > MAX_PROCESS);
    struct proc_struct *proc;
    list_entry_t *list = &proc_list, *le;
    static int next_safe = MAX_PID, last_pid = MAX_PID;
    if (++ last_pid >= MAX_PID) {
        last_pid = 1;
        goto inside;
    }
    if (last_pid >= next_safe) {
    inside:
        next_safe = MAX_PID;
    repeat:
        le = list;
        while ((le = list_next(le)) != list) {
            proc = le2proc(le, list_link);
            if (proc->pid == last_pid) {
                if (++ last_pid >= next_safe) {
                    if (last_pid >= MAX_PID) {
                        last_pid = 1;
                    }
                    next_safe = MAX_PID;
                    goto repeat;
                }
            }
            else if (proc->pid > last_pid && next_safe > proc->pid) {
                next_safe = proc->pid;
            }
        }
    }
    return last_pid;
}

// proc_run - make process "proc" running on cpu
// NOTE: before call switch_to, should load  base addr of "proc"'s new PDT
void
proc_run(struct proc_struct *proc) {
    if (proc != current) {
        // LAB4:EXERCISE3 YOUR CODE
        /*
        * Some Useful MACROs, Functions and DEFINEs, you can use them in below implementation.
        * MACROs or Functions:
        *   local_intr_save():        Disable interrupts
        *   local_intr_restore():     Enable Interrupts
        *   lcr3():                   Modify the value of CR3 register
        *   switch_to():              Context switching between two processes
        */
       bool intr_flag;
       struct proc_struct *prev = current, *next = proc;
       local_intr_save(intr_flag);
       {
            current = proc;
            lcr3(next->cr3);
            switch_to(&(prev->context), &(next->context));
       }
       local_intr_restore(intr_flag);
    }
}

// forkret -- the first kernel entry point of a new thread/process
// NOTE: the addr of forkret is setted in copy_thread function
//       after switch_to, the current proc will execute here.
static void
forkret(void) {
    forkrets(current->tf);
}

// hash_proc - add proc into proc hash_list
static void
hash_proc(struct proc_struct *proc) {
    list_add(hash_list + pid_hashfn(proc->pid), &(proc->hash_link));
}

// unhash_proc - delete proc from proc hash_list
static void
unhash_proc(struct proc_struct *proc) {
    list_del(&(proc->hash_link));
}

// find_proc - find proc frome proc hash_list according to pid
struct proc_struct *
find_proc(int pid) {
    if (0 < pid && pid < MAX_PID) {
        list_entry_t *list = hash_list + pid_hashfn(pid), *le = list;
        while ((le = list_next(le)) != list) {
            struct proc_struct *proc = le2proc(le, hash_link);
            if (proc->pid == pid) {
                return proc;
            }
        }
    }
    return NULL;
}

// kernel_thread - create a kernel thread using "fn" function
// NOTE: the contents of temp trapframe tf will be copied to 
//       proc->tf in do_fork-->copy_thread function
int
kernel_thread(int (*fn)(void *), void *arg, uint32_t clone_flags) {
    struct trapframe tf;
    memset(&tf, 0, sizeof(struct trapframe));
    tf.gpr.s0 = (uintptr_t)fn;
    tf.gpr.s1 = (uintptr_t)arg;
    tf.status = (read_csr(sstatus) | SSTATUS_SPP | SSTATUS_SPIE) & ~SSTATUS_SIE;
    tf.epc = (uintptr_t)kernel_thread_entry;
    return do_fork(clone_flags | CLONE_VM, 0, &tf);
}

// setup_kstack - alloc pages with size KSTACKPAGE as process kernel stack
static int
setup_kstack(struct proc_struct *proc) {
    struct Page *page = alloc_pages(KSTACKPAGE);
    if (page != NULL) {
        proc->kstack = (uintptr_t)page2kva(page);
        return 0;
    }
    return -E_NO_MEM;
}

// put_kstack - free the memory space of process kernel stack
static void
put_kstack(struct proc_struct *proc) {
    free_pages(kva2page((void *)(proc->kstack)), KSTACKPAGE);
}

// setup_pgdir - alloc one page as PDT
static int
setup_pgdir(struct mm_struct *mm) {
    struct Page *page;
    if ((page = alloc_page()) == NULL) {
        return -E_NO_MEM;
    }
    pde_t *pgdir = page2kva(page);
    memcpy(pgdir, boot_pgdir, PGSIZE);

    mm->pgdir = pgdir;
    return 0;
}

// put_pgdir - free the memory space of PDT
static void
put_pgdir(struct mm_struct *mm) {
    free_page(kva2page(mm->pgdir));
}

// copy_mm - process "proc" duplicate OR share process "current"'s mm according clone_flags
//         - if clone_flags & CLONE_VM, then "share" ; else "duplicate"
static int
copy_mm(uint32_t clone_flags, struct proc_struct *proc) {
    // 获取当前进程的内存管理结构
    struct mm_struct *mm, *oldmm = current->mm;

    // 如果当前进程是内核线程，则没有内存管理结构，直接返回成功
    if (oldmm == NULL) {
        return 0;
    }

    //clone_flags 表示进程或线程创建的各种选项，通常是多个标志的组合
    //CLONE_VM 表示子进程/线程与父进程共享内存地址空间，是多线程环境中的关键标志
    //如果结果不为 0，说明 CLONE_VM 被设置，表示需要共享虚拟内存。
    // 检查是否需要共享内存空间
    if (clone_flags & CLONE_VM) {
        // 设置为共享内存，子进程直接指向父进程的内存管理结构
        mm = oldmm;
        goto good_mm;
    }

    // 初始化失败返回值
    int ret = -E_NO_MEM;

    // 创建新的内存管理结构
    if ((mm = mm_create()) == NULL) {
        goto bad_mm;// 如果创建失败，返回内存不足错误
    }

    // 分配新的页目录并初始化
    if (setup_pgdir(mm) != 0) {
        goto bad_pgdir_cleanup_mm;// 如果页目录分配失败，清理内存管理结构
    }

    // 锁定父进程的内存管理结构，防止并发修改
    lock_mm(oldmm);
    {
        ret = dup_mmap(mm, oldmm);// 拷贝父进程的内存映射区域到新进程，里面还进行了copy_range，复制内存页
    }
    // 解锁内存管理结构
    unlock_mm(oldmm);

    // 检查是否拷贝内存映射区域成功
    if (ret != 0) {
        // 拷贝失败，清理页表和内存管理结构
        goto bad_dup_cleanup_mmap;
    }

good_mm:
    // 增加内存管理结构的引用计数
    mm_count_inc(mm);

    // 将新进程的内存管理结构指向创建的结构                             
    proc->mm = mm;

    // 设置进程的页目录物理地址
    proc->cr3 = PADDR(mm->pgdir);//将页目录虚拟地址转换为（通过计算）物理地址，存入cr3中（cr3存储页目录表的物理地址，页目录表用于存储虚拟地址和物理地址的映射关系）
    return 0;//内核采用简单的线性映射，仅适用于内核空间的固定地址范围。页表用于管理用户态进程和动态内存，支持更灵活和复杂的内存管理机制。

// 错误处理部分，清理已分配的资源，防止内存泄漏

bad_dup_cleanup_mmap:
    // 清理内存映射区域
    exit_mmap(mm);
    // 释放页目录占用的内存页
    put_pgdir(mm);

bad_pgdir_cleanup_mm:
    // 销毁内存管理结构
    mm_destroy(mm);

bad_mm:
    // 返回错误代码
    return ret;
}

// copy_thread - setup the trapframe on the  process's kernel stack top and
//             - setup the kernel entry point and stack of process
static void
copy_thread(struct proc_struct *proc, uintptr_t esp, struct trapframe *tf) {
    proc->tf = (struct trapframe *)(proc->kstack + KSTACKSIZE) - 1;
    *(proc->tf) = *tf;

    // Set a0 to 0 so a child process knows it's just forked
    proc->tf->gpr.a0 = 0;
    proc->tf->gpr.sp = (esp == 0) ? (uintptr_t)proc->tf : esp;

    proc->context.ra = (uintptr_t)forkret;
    proc->context.sp = (uintptr_t)(proc->tf);
}

/* do_fork - 父进程为新子进程创建一个副本
 * @clone_flags: 用于指导如何克隆子进程的标志
 * @stack: 父进程的用户栈指针。如果stack == 0，表示要创建一个内核线程。
 * @tf: 将被复制到子进程的proc->tf的trapframe信息
 */
int
do_fork(uint32_t clone_flags, uintptr_t stack, struct trapframe *tf) {
    int ret = -E_NO_FREE_PROC; // 初始化返回值为进程不可用错误
    struct proc_struct *proc;
    
    if (nr_process >= MAX_PROCESS) { // 如果进程数量超过最大限制，则不能再创建新进程
        goto fork_out;
    }
    
    ret = -E_NO_MEM; // 初始化内存不足的错误代码
    
    // 以下是创建新进程的关键步骤：
    // 1. 调用alloc_proc分配proc_struct
    // 2. 调用setup_kstack为子进程分配内核栈
    // 3. 调用copy_mm根据clone_flags复制或共享内存
    // 4. 调用copy_thread设置子进程的tf和内核栈
    // 5. 将proc_struct插入hash_list和proc_list
    // 6. 调用wakeup_proc将新进程设置为可运行状态
    // 7. 设置ret值为子进程的pid

    // 分配一个新的进程结构体proc
    if ((proc = alloc_proc()) == NULL) {
        goto fork_out; // 如果分配失败，跳转到退出处理
    }
    
    proc->parent = current; // 设置子进程的父进程为当前进程
    assert(current->wait_state == 0); // 确保当前进程的等待状态为0

    // 为子进程分配内核栈
    if (setup_kstack(proc) != 0) {
        goto bad_fork_cleanup_proc; // 如果内存分配失败，跳转到清理处理
    }
    // if(copy_mm(clone_flags, proc) != 0)
    // {
    //     goto bad_fork_cleanup_kstack;
    // }

    // 如果需要共享内存，则使用cow_copy_mm复制内存
    if (cow_copy_mm(proc) != 0) {
        goto bad_fork_cleanup_kstack; // 如果内存复制失败，清理内核栈并退出
    }

    // 设置子进程的trapframe
    copy_thread(proc, stack, tf);

    bool intr_flag;
    local_intr_save(intr_flag); // 保存当前中断状态
    
    {
        int pid = get_pid(); // 分配一个新的进程ID
        proc->pid = pid; // 设置子进程的pid
        hash_proc(proc); // 将子进程添加到进程哈希表
        set_links(proc); // 设置子进程的关系链接（父子进程关系）
    }

    local_intr_restore(intr_flag); // 恢复中断状态
    
    // 将子进程设置为可运行状态
    wakeup_proc(proc);
    
    // 设置返回值为子进程的pid
    ret = proc->pid;

fork_out:
    return ret; // 返回子进程的pid

// 错误处理：如果创建内核栈失败，释放相关资源
bad_fork_cleanup_kstack:
    put_kstack(proc); // 释放内核栈
bad_fork_cleanup_proc:
    kfree(proc); // 释放进程结构体
    goto fork_out; // 跳转到退出处理
}





// do_exit - 由 sys_exit 调用
//   1. 调用 exit_mmap、put_pgdir 和 mm_destroy 来释放进程几乎所有的内存空间
//   2. 将进程状态设置为 PROC_ZOMBIE，然后调用 wakeup_proc(parent) 请求父进程回收自身。
//   3. 调用调度器切换到其他进程
int
do_exit(int error_code) {
    // 检查当前进程是否为idleproc或initproc，如果是，发出panic
    if (current == idleproc) {
        panic("idleproc exit.\n");
    }
    if (current == initproc) {
        panic("initproc exit.\n");
    }
    // 获取当前进程的内存管理结构mm
    struct mm_struct *mm = current->mm;
    // 如果mm不为空，说明是用户进程
    if (mm != NULL) {
        // 切换到内核页表，确保接下来的操作在内核空间执行
        lcr3(boot_cr3);
        // 如果mm引用计数减到0，说明没有其他进程共享此mm
        if (mm_count_dec(mm) == 0) {
            // 释放用户虚拟内存空间相关的资源
            exit_mmap(mm);
            put_pgdir(mm);
            mm_destroy(mm);
        }
        // 将当前进程的mm设置为NULL，表示资源已经释放
        current->mm = NULL;
    }
    // 设置进程状态为PROC_ZOMBIE，表示进程已退出
    current->state = PROC_ZOMBIE;
    current->exit_code = error_code;
    bool intr_flag;
    struct proc_struct *proc;
    // 关中断
    local_intr_save(intr_flag);
    {
        // 获取当前进程的父进程
        proc = current->parent;
        // 如果父进程处于等待子进程状态，则唤醒父进程
        if (proc->wait_state == WT_CHILD) {
            wakeup_proc(proc);
        }
        // 遍历当前进程的所有子进程
        while (current->cptr != NULL) {
            proc = current->cptr;
            current->cptr = proc->optr;
            
            // 设置子进程的父进程为initproc，并加入initproc的子进程链表
            proc->yptr = NULL;
            if ((proc->optr = initproc->cptr) != NULL) {
                initproc->cptr->yptr = proc;
            }
            
            proc->parent = initproc;
            initproc->cptr = proc;
            // 如果子进程也处于退出状态，唤醒initproc
            if (proc->state == PROC_ZOMBIE) {
                if (initproc->wait_state == WT_CHILD) {
                    wakeup_proc(initproc);
                }
            }
        }
    }
    // 开中断
    local_intr_restore(intr_flag);
    // 调用调度器，选择新的进程执行
    schedule();
    // 如果执行到这里，表示代码执行出现错误，发出panic
    panic("do_exit will not return!! %d.\n", current->pid);
}

/* load_icode - load the content of binary program(ELF format) as the new content of current process
 * @binary:  the memory addr of the content of binary program
 * @size:  the size of the content of binary program
 */
static int load_icode(unsigned char *binary, size_t size) {
    // 确保当前进程没有内存管理结构
    if (current->mm != NULL) {
        panic("load_icode: current->mm must be empty.\n"); // 如果当前进程已有内存管理结构，报错
    }

    int ret = -E_NO_MEM; // 默认返回内存分配错误
    struct mm_struct *mm;
    
    //(1) 创建新的内存管理结构
    if ((mm = mm_create()) == NULL) {
        goto bad_mm; // 如果创建失败，跳转到错误处理
    }

    //(2) 创建新的页目录，并设置mm->pgdir为内核虚拟地址
    if (setup_pgdir(mm) != 0) {
        goto bad_pgdir_cleanup_mm; // 如果页目录设置失败，清理并跳转到错误处理
    }

    //(3) 复制TEXT/DATA部分，并构建BSS段到进程的内存空间
    struct Page *page;
    //(3.1) 获取二进制文件头（ELF格式）
    struct elfhdr *elf = (struct elfhdr *)binary;
    //(3.2) 获取程序段头表的位置
    struct proghdr *ph = (struct proghdr *)(binary + elf->e_phoff);
    //(3.3) 检查ELF文件是否有效
    if (elf->e_magic != ELF_MAGIC) {
        ret = -E_INVAL_ELF; // 如果ELF标识无效，返回错误
        goto bad_elf_cleanup_pgdir; // 清理页目录并跳转到错误处理
    }

    uint32_t vm_flags, perm;
    struct proghdr *ph_end = ph + elf->e_phnum;
    for (; ph < ph_end; ph++) {
        //(3.4) 遍历每个程序段头
        if (ph->p_type != ELF_PT_LOAD) { 
            continue; // 只处理加载类型的程序段
        }
        if (ph->p_filesz > ph->p_memsz) {
            ret = -E_INVAL_ELF; // 如果文件大小大于内存大小，返回错误
            goto bad_cleanup_mmap; // 清理内存映射并跳转到错误处理
        }
        if (ph->p_filesz == 0) {
            // 如果文件大小为0，跳过该段
            continue;
        }

        //(3.5) 使用mm_map函数为程序段设置新的虚拟内存区域
        vm_flags = 0, perm = PTE_U | PTE_V; // 默认权限标志
        if (ph->p_flags & ELF_PF_X) vm_flags |= VM_EXEC; // 如果有执行权限，设置执行标志
        if (ph->p_flags & ELF_PF_W) vm_flags |= VM_WRITE; // 如果有写权限，设置写标志
        if (ph->p_flags & ELF_PF_R) vm_flags |= VM_READ; // 如果有读权限，设置读标志
        // 根据RISC-V的需求修改权限位
        if (vm_flags & VM_READ) perm |= PTE_R;
        if (vm_flags & VM_WRITE) perm |= (PTE_W | PTE_R);
        if (vm_flags & VM_EXEC) perm |= PTE_X;
        if ((ret = mm_map(mm, ph->p_va, ph->p_memsz, vm_flags, NULL)) != 0) {
            goto bad_cleanup_mmap; // 如果内存映射失败，跳转到错误处理
        }

        unsigned char *from = binary + ph->p_offset;
        size_t off, size;
        uintptr_t start = ph->p_va, end, la = ROUNDDOWN(start, PGSIZE);

        ret = -E_NO_MEM; // 默认返回内存分配失败

        //(3.6) 为每个程序段分配内存并复制内容
        end = ph->p_va + ph->p_filesz;
        //(3.6.1) 复制TEXT/DATA段内容到进程内存
        while (start < end) {
            if ((page = pgdir_alloc_page(mm->pgdir, la, perm)) == NULL) {
                goto bad_cleanup_mmap; // 如果内存分配失败，跳转到错误处理
            }
            off = start - la, size = PGSIZE - off, la += PGSIZE;
            if (end < la) {
                size -= la - end;
            }
            memcpy(page2kva(page) + off, from, size); // 将文件内容复制到内存
            start += size, from += size;
        }

       //(3.6.2) 为BSS段分配并清零内存
        end = ph->p_va + ph->p_memsz; // 计算BSS段的结束地址
        if (start < la) { // 检查是否有剩余的BSS段内存需要处理
            if (start == end) {
                continue; // 如果BSS段的开始地址和结束地址相同，跳过
            }
            // 计算当前内存页中未使用的空间（off），以及需要分配的大小（size）
            off = start + PGSIZE - la, size = PGSIZE - off;
            if (end < la) {
                size -= la - end; // 如果BSS段结束地址在当前页之前，调整分配的内存大小
            }
            memset(page2kva(page) + off, 0, size); // 将当前页的剩余部分清零，初始化BSS段内存
            start += size; // 更新start为已分配并清零的内存区域的结束位置
            assert((end < la && start == end) || (end >= la && start == la)); // 确保内存区域被正确更新
        }
        while (start < end) { // 如果还有剩余的内存需要分配并清零
            // 分配新的内存页
            if ((page = pgdir_alloc_page(mm->pgdir, la, perm)) == NULL) {
                goto bad_cleanup_mmap; // 如果内存分配失败，跳转到错误处理
            }
            // 计算当前页的偏移量（off）和需要分配的大小（size）
            off = start - la, size = PGSIZE - off, la += PGSIZE;
            if (end < la) {
                size -= la - end; // 如果BSS段结束地址在当前页之前，调整分配的内存大小
            }
            memset(page2kva(page) + off, 0, size); // 将当前页的内存清零，初始化BSS段内存
            start += size; // 更新start为已分配并清零的内存区域的结束位置
        }

    //(4) 为用户栈分配内存
    vm_flags = VM_READ | VM_WRITE | VM_STACK;
    if ((ret = mm_map(mm, USTACKTOP - USTACKSIZE, USTACKSIZE, vm_flags, NULL)) != 0) {
        goto bad_cleanup_mmap; // 如果栈内存分配失败，跳转到错误处理
    }
    assert(pgdir_alloc_page(mm->pgdir, USTACKTOP - PGSIZE, PTE_USER) != NULL); // 为栈分配页
    assert(pgdir_alloc_page(mm->pgdir, USTACKTOP - 2 * PGSIZE, PTE_USER) != NULL);
    assert(pgdir_alloc_page(mm->pgdir, USTACKTOP - 3 * PGSIZE, PTE_USER) != NULL);
    assert(pgdir_alloc_page(mm->pgdir, USTACKTOP - 4 * PGSIZE, PTE_USER) != NULL);

    //(5) 设置当前进程的内存管理结构，cr3寄存器，切换到新的页目录
    mm_count_inc(mm); // 增加内存管理结构的引用计数
    current->mm = mm; // 设置进程的内存管理结构
    current->cr3 = PADDR(mm->pgdir); // 设置CR3寄存器为页目录的物理地址
    lcr3(PADDR(mm->pgdir)); // 切换到新的页目录

    //(6) 为用户环境设置陷阱帧（trapframe）
    struct trapframe *tf = current->tf;
    uintptr_t sstatus = tf->status; // 保留sstatus
    memset(tf, 0, sizeof(struct trapframe)); // 清空陷阱帧

    // 设置用户程序的trapframe
    tf->gpr.sp = USTACKTOP; // 设置栈指针
    tf->epc = elf->e_entry; // 设置程序入口地址
    tf->status = sstatus & ~(SSTATUS_SPP | SSTATUS_SPIE); // 设置状态寄存器，去掉内核模式标志——SPP位清零，代表异常来自用户态，之后需要返回用户态；SPIE位清零，表示不启用中断

    ret = 0; // 成功
out:
    return ret; // 返回结果

bad_cleanup_mmap:
    exit_mmap(mm); // 清理内存映射
bad_elf_cleanup_pgdir:
    put_pgdir(mm); // 清理页目录
bad_pgdir_cleanup_mm:
    mm_destroy(mm); // 销毁内存管理结构
bad_mm:
    goto out; // 跳转到函数尾部
}

// do_execve - call exit_mmap(mm)&put_pgdir(mm) to reclaim memory space of current process
//           - call load_icode to setup new memory space accroding binary prog.
//加载一个新的程序的系统调用
//该函数涉及对当前进程的内存空间回收、清理旧的地址空间，并将新的可执行文件加载到当前进程的内存空间中
int do_execve(const char *name, size_t len, unsigned char *binary, size_t size) {
    struct mm_struct *mm = current->mm; // 获取当前进程的内存管理结构

    if (!user_mem_check(mm, (uintptr_t)name, len, 0)) { // 检查程序名的内存空间是否可访问
        return -E_INVAL; // 如果不可访问，返回无效错误
    }

    if (len > PROC_NAME_LEN) { // 如果程序名长度超过限制
        len = PROC_NAME_LEN; // 限制程序名长度
    }

    char local_name[PROC_NAME_LEN + 1]; // 创建本地存储程序名的缓冲区
    memset(local_name, 0, sizeof(local_name)); // 初始化缓冲区
    memcpy(local_name, name, len); // 将程序名复制到本地缓冲区

    if (mm != NULL) { // 如果当前进程有内存管理结构
        cputs("mm != NULL"); // 打印调试信息
        lcr3(boot_cr3); // 切换到启动页目录，防止释放当前进程的内存时干扰其他进程

        if (mm_count_dec(mm) == 0) { // 如果内存描述符引用计数降到0
            exit_mmap(mm); // 释放当前进程的内存映射
            put_pgdir(mm); // 清理页目录
            mm_destroy(mm); // 销毁内存管理结构
        }
        current->mm = NULL; // 清空当前进程的内存管理结构
    }

    int ret; 
    if ((ret = load_icode(binary, size)) != 0) { // 加载新的程序
        goto execve_exit; // 如果加载失败，跳转到退出处理
    }

    set_proc_name(current, local_name); // 设置进程名称
    return 0; // 返回0表示成功

execve_exit:
    do_exit(ret); // 执行进程退出，返回错误码
    panic("already exit: %e.\n", ret); // 如果程序加载失败，输出错误信息并引发内核崩溃
}

// do_yield - ask the scheduler to reschedule
int
do_yield(void) {
    current->need_resched = 1;
    return 0;
}

// do_wait - 等待一个或任意一个状态为 PROC_ZOMBIE 的子进程，并释放该子进程的内核栈内存空间
//         - 释放该子进程的 proc 结构体。
// NOTE: 只有在 do_wait 函数执行完后，子进程的所有资源才会被释放。
int
do_wait(int pid, int *code_store) {
    struct mm_struct *mm = current->mm;
    
    // 检查 code_store 是否有效且可以访问
    if (code_store != NULL) {
        if (!user_mem_check(mm, (uintptr_t)code_store, sizeof(int), 1)) {
            return -E_INVAL;  // 如果无法访问 code_store，返回无效错误
        }
    }

    struct proc_struct *proc;
    bool intr_flag, haskid;
repeat:
    haskid = 0;

    // 如果 pid 非零，查找指定 pid 的子进程
    if (pid != 0) {
        proc = find_proc(pid);  // 查找进程 pid 对应的进程
        if (proc != NULL && proc->parent == current) {  // 如果该进程是当前进程的子进程
            haskid = 1;
            if (proc->state == PROC_ZOMBIE) {  // 如果子进程的状态是 PROC_ZOMBIE
                goto found;  // 进入 found 标签
            }
        }
    }
    // 如果 pid 为零，检查当前进程的所有子进程
    else {
        proc = current->cptr;
        for (; proc != NULL; proc = proc->optr) {
            haskid = 1;
            if (proc->state == PROC_ZOMBIE) {  // 如果有状态为 PROC_ZOMBIE 的子进程
                goto found;  // 进入 found 标签
            }
        }
    }

    // 如果找到了子进程，但没有状态为 PROC_ZOMBIE 的子进程
    if (haskid) {
        current->state = PROC_SLEEPING;  // 将当前进程状态设置为 PROC_SLEEPING（休眠状态）
        current->wait_state = WT_CHILD;  // 设置当前进程的等待状态为 WT_CHILD
        schedule();  // 调用调度器
        if (current->flags & PF_EXITING) {  // 如果当前进程正在退出
            do_exit(-E_KILLED);  // 执行退出操作
        }
        goto repeat;  // 重复执行，等待子进程
    }

    // 如果没有找到子进程
    return -E_BAD_PROC;  // 返回无效进程错误

found:
    // 如果找到的子进程是 idleproc 或 initproc，报错并退出
    if (proc == idleproc || proc == initproc) {
        panic("wait idleproc or initproc.\n");
    }

    // 如果传入了 code_store，将子进程的退出代码存储到 code_store 中
    if (code_store != NULL) {
        *code_store = proc->exit_code;
    }

    // 禁止中断，进行进程资源回收
    local_intr_save(intr_flag);
    {
        unhash_proc(proc);  // 从进程哈希表中删除该进程
        remove_links(proc);  // 移除进程链表中的关系
    }
    local_intr_restore(intr_flag);  // 恢复中断

    // 释放子进程的内核栈和 proc 结构体内存
    put_kstack(proc);
    kfree(proc);

    return 0;  // 成功返回
}


// do_kill - 通过设置目标进程的 flags 为 PF_EXITING 来终止指定 pid 的进程
int
do_kill(int pid) {
    struct proc_struct *proc;

    // 查找指定 pid 的进程
    if ((proc = find_proc(pid)) != NULL) {
        // 如果目标进程没有被标记为正在退出
        if (!(proc->flags & PF_EXITING)) {
            proc->flags |= PF_EXITING;  // 将目标进程的 flags 设置为 PF_EXITING，标记为正在退出

            // 如果目标进程的等待状态为 WT_INTERRUPTED（被中断）
            if (proc->wait_state & WT_INTERRUPTED) {
                wakeup_proc(proc);  // 唤醒该进程
            }
            return 0;  // 返回 0 表示成功杀死进程
        }
        return -E_KILLED;  // 如果目标进程已经在退出状态，返回已终止的错误
    }

    return -E_INVAL;  // 如果找不到目标进程，返回无效进程错误
}


// kernel_execve - 执行SYS_exec系统调用以启动一个由user_main内核线程调用的用户程序
static int
kernel_execve(const char *name, unsigned char *binary, size_t size) {
    int64_t ret = 0, len = strlen(name); // 计算程序名的长度并初始化返回值为0
    // 通过汇编代码执行SYS_exec系统调用
    asm volatile(
        "li a0, %1\n" // 将系统调用号（SYS_exec）加载到寄存器a0
        "lw a1, %2\n" // 将程序名（name）的地址加载到寄存器a1
        "lw a2, %3\n" // 将程序名的长度（len）加载到寄存器a2
        "lw a3, %4\n" // 将二进制程序文件（binary）的地址加载到寄存器a3
        "lw a4, %5\n" // 将二进制程序的大小（size）加载到寄存器a4
        "li a7, 10\n" // 将系统调用号10（即SYS_exec）加载到寄存器a7
        "ebreak\n" // 执行中断（触发系统调用）
        "sw a0, %0\n" // 将返回值（a0寄存器中的值）存储到ret变量中
        : "=m"(ret) // 输出操作数，将寄存器a0的值存入ret
        : "i"(SYS_exec), "m"(name), "m"(len), "m"(binary), "m"(size) // 输入操作数：系统调用号、参数name、len、binary和size
        : "memory" // 告诉编译器该汇编代码可能会修改内存
    );
    
    cprintf("ret = %d\n", ret); // 打印返回值
    return ret; // 返回执行结果
}


#define __KERNEL_EXECVE(name, binary, size) ({                          \
            cprintf("kernel_execve: pid = %d, name = \"%s\".\n",        \
                    current->pid, name);                                \
            kernel_execve(name, binary, (size_t)(size));                \
        })

#define KERNEL_EXECVE(x) ({                                             \
            extern unsigned char _binary_obj___user_##x##_out_start[],  \
                _binary_obj___user_##x##_out_size[];                    \
            __KERNEL_EXECVE(#x, _binary_obj___user_##x##_out_start,     \
                            _binary_obj___user_##x##_out_size);         \
        })

#define __KERNEL_EXECVE2(x, xstart, xsize) ({                           \
            extern unsigned char xstart[], xsize[];                     \
            __KERNEL_EXECVE(#x, xstart, (size_t)xsize);                 \
        })

#define KERNEL_EXECVE2(x, xstart, xsize)        __KERNEL_EXECVE2(x, xstart, xsize)

// user_main - kernel thread used to exec a user program
static int
user_main(void *arg) {
#ifdef TEST
    KERNEL_EXECVE2(TEST, TESTSTART, TESTSIZE);
#else
    KERNEL_EXECVE(exit);
#endif
    panic("user_main execve failed.\n");
}

// init_main - the second kernel thread used to create user_main kernel threads
static int
init_main(void *arg) {
    size_t nr_free_pages_store = nr_free_pages();
    size_t kernel_allocated_store = kallocated();

    int pid = kernel_thread(user_main, NULL, 0);
    if (pid <= 0) {
        panic("create user_main failed.\n");
    }

    while (do_wait(0, NULL) == 0) {//等待子进程退出，也就是等待user_main()退出。
        schedule();
    }

    cprintf("all user-mode processes have quit.\n");
    assert(initproc->cptr == NULL && initproc->yptr == NULL && initproc->optr == NULL);
    assert(nr_process == 2);
    assert(list_next(&proc_list) == &(initproc->list_link));
    assert(list_prev(&proc_list) == &(initproc->list_link));

    cprintf("init check memory pass.\n");
    return 0;
}

// proc_init - set up the first kernel thread idleproc "idle" by itself and 
//           - create the second kernel thread init_main
void
proc_init(void) {
    int i;

    list_init(&proc_list);
    for (i = 0; i < HASH_LIST_SIZE; i ++) {
        list_init(hash_list + i);
    }

    if ((idleproc = alloc_proc()) == NULL) {
        panic("cannot alloc idleproc.\n");
    }

    idleproc->pid = 0;
    idleproc->state = PROC_RUNNABLE;
    idleproc->kstack = (uintptr_t)bootstack;
    idleproc->need_resched = 1;
    set_proc_name(idleproc, "idle");
    nr_process ++;

    current = idleproc;

    int pid = kernel_thread(init_main, NULL, 0);
    if (pid <= 0) {
        panic("create init_main failed.\n");
    }

    initproc = find_proc(pid);
    set_proc_name(initproc, "init");

    assert(idleproc != NULL && idleproc->pid == 0);
    assert(initproc != NULL && initproc->pid == 1);
}

// cpu_idle - at the end of kern_init, the first kernel thread idleproc will do below works
void
cpu_idle(void) {
    while (1) {
        if (current->need_resched) {
            schedule();
        }
    }
}

