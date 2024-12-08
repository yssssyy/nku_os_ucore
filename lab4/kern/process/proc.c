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

        proc->state = PROC_UNINIT;//设置进程为“初始”态
        proc->pid = -1;//设置进程pid的未初始化值
        proc->runs = 0;
        proc->kstack = 0;
        proc->need_resched = 0;
        proc->parent = NULL;
        proc->mm = NULL;
        memset(&(proc->context) ,0,sizeof(struct context));
        proc->tf = NULL;
        proc->cr3 = boot_cr3;//使用内核页目录表的基址
        proc->flags = 0;
        memset(proc->name,0,PROC_NAME_LEN+1);
             
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

// get_pid - alloc a unique pid for process
//通过遍历进程链表来查找下一个可用的 PID，避免分配给已经存在的进程 PID
static int
get_pid(void) {
    static_assert(MAX_PID > MAX_PROCESS);//确保 PID 数量足够分配给所有进程
    struct proc_struct *proc;
    list_entry_t *list = &proc_list, *le;
//last_pid 用于记录 上一个分配的进程 ID。每次生成新的 PID 时，它会递增，并检查这个 PID 是否已经被使用。
//next_safe 用于记录 下一个“安全的”可用 PID。它的作用是帮助减少对已经被占用的 PID 进行重复检查。
    static int next_safe = MAX_PID, last_pid = MAX_PID;
    //递增 last_pid，检查其是否超过 MAX_PID。如果超过，则重置为 1（PID 不能为 0），然后跳转到 inside 标签。
    if (++ last_pid >= MAX_PID) {
        last_pid = 1;
        goto inside;
    }
    if (last_pid >= next_safe) {
    inside:
        next_safe = MAX_PID;
    repeat://遍历进程链表，寻找可用的 PID
        le = list;
//遍历进程链表，检查是否有进程已经占用了该 PID。如果发现 PID 被占用，它会继续递增并检查，直到找到一个未使用的 PID。
        while ((le = list_next(le)) != list) {
            proc = le2proc(le, list_link);//获取链表节点对应的进程结构
//检查 last_pid 是否已经被使用：如果当前进程的 PID 与 last_pid 相同，说明该 PID 已经被分配，需跳到下一个 PID，更新 last_pid 并重新检查。
            if (proc->pid == last_pid) {
//如果递增后的 last_pid 大于或等于 next_safe，说明我们已经遍历了当前 "安全" PID 范围中的所有可能值，
//需要重新检查更大的 PID 范围。此时会更新 next_safe 为一个更大的值，并重新开始遍历链表。
                if (++ last_pid >= next_safe) {
//如果递增后的值超过 MAX_PID，则 last_pid 会重新回到 1（因为 PID 为 0 是无效的)
                    if (last_pid >= MAX_PID) {
                        last_pid = 1;
                    }
                    next_safe = MAX_PID;
                    goto repeat;
                }
            }
//如果当前进程的 PID 大于 last_pid 且小于 next_safe，
//更新 next_safe 为该进程的 PID，表示下一个可用的 PID 应小于该进程的 PID。
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
        /*
        - 检查要切换的进程是否与当前正在运行的进程相同，如果相同则不需要切换。
        - 禁用中断。你可以使用/kern/sync/sync.h中定义好的宏local_intr_save(x)和local_intr_restore(x)来实现关、开中断。
        - 切换当前进程为要运行的进程。
        - 切换页表，以便使用新进程的地址空间。/libs/riscv.h中提供了lcr3(unsigned int cr3)函数，可实现修改CR3寄存器值的功能。
        - 实现上下文切换。/kern/process中已经预先编写好了switch.S，其中定义了switch_to()函数。可实现两个进程的context切换。
        - 允许中断。
        */
        bool intr_flag;
        struct proc_struct *prev =current,*next = proc;
        local_intr_save(intr_flag);
        {
            current = proc;
            lcr3(proc->cr3);
            switch_to(&(prev->context),&(next->context));
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
    /*trapentry.S:
    forkrets:
    # set stack to this new process's trapframe
    move sp, a0
    j __trapret
    */
}

// hash_proc - add proc into proc hash_list
static void
hash_proc(struct proc_struct *proc) {
    list_add(hash_list + pid_hashfn(proc->pid), &(proc->hash_link));
}

// find_proc - find proc frome proc hash_list according to pid
struct proc_struct *
find_proc(int pid) {
    if (0 < pid && pid < MAX_PID) {
        //按 pid 计算哈希值，将进程分配到不同的桶中以提高查找效率
        list_entry_t *list = hash_list + pid_hashfn(pid), *le = list;
        //遍历哈希链表
        while ((le = list_next(le)) != list) {
            //将链表节点 le 转换为包含它的 proc_struct 结构体指针
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
    //对程序的一些上下文初始化
    struct trapframe tf;//保存了进程的中断帧,用于存储与进程上下文相关的信息，特别是寄存器状态。
    memset(&tf, 0, sizeof(struct trapframe));
    //// 设置内核线程的参数和函数指针
    tf.gpr.s0 = (uintptr_t)fn;// s0 寄存器保存函数指针
    tf.gpr.s1 = (uintptr_t)arg;// s1 寄存器保存函数参数

    // 设置 trapframe 中的 status 寄存器（SSTATUS）
    //实现特权级别切换、保留中断使能状态并禁用中断的操作。
    // SSTATUS_SPP：Supervisor Previous Privilege（设置为 supervisor 模式，因为这是一个内核线程）,表示内核模式的状态（S 是超级用户模式）。
    // SSTATUS_SPIE：Supervisor Previous Interrupt Enable（设置为启用中断，因为这是一个内核线程）
    // SSTATUS_SIE：Supervisor Interrupt Enable（设置为禁用外部中断，因为我们不希望该线程被中断）
    tf.status = (read_csr(sstatus) | SSTATUS_SPP | SSTATUS_SPIE) & ~SSTATUS_SIE;

    //process/entry.S中有kernel_thread_entry
    tf.epc = (uintptr_t)kernel_thread_entry;//设置 epc（程序计数器）寄存器，指定内核线程的起始地址,实际上是将pc指针指向它(*trapentry.S会用到)
    // 使用 do_fork 创建一个新进程（内核线程），这样才真正用设置的tf创建新进程。
    //把中断帧的指针传递给do_fork函数，而do_fork函数会调用copy_thread函数来在新创建的进程内核栈上专门给进程的中断帧分配一块空间。
    //CLONE_VM 是一个标志，表示新线程与父线程共享虚拟地址空间（不复制页表）
    return do_fork(clone_flags | CLONE_VM, 0, &tf);//初始化新线程的状态，并启动该线程执行
}
// setup_kstack - alloc pages with size KSTACKPAGE as process kernel stack
//为进程分配大小为 KSTACKPAGE 的内核栈页面
static int
setup_kstack(struct proc_struct *proc) {
    struct Page *page = alloc_pages(KSTACKPAGE);
    if (page != NULL) {
        // 将分配到的页面映射到进程的内核栈指针
        proc->kstack = (uintptr_t)page2kva(page);
        return 0;
    }
    return -E_NO_MEM;
}

// put_kstack - free the memory space of process kernel stack释放某个进程的内核栈 (kstack) 以回收内存资源
static void
put_kstack(struct proc_struct *proc) {
    //kva2page将进程的内核栈虚拟地址 proc->kstack 转换为指向对应物理页的描述结构体 struct page *
    //KSTACKPAGE：表示需要释放的页数量
    free_pages(kva2page((void *)(proc->kstack)), KSTACKPAGE);
}

// copy_mm - process "proc" duplicate OR share process "current"'s mm according clone_flags
//         - if clone_flags & CLONE_VM, then "share" ; else "duplicate"
static int
copy_mm(uint32_t clone_flags, struct proc_struct *proc) {
    assert(current->mm == NULL);
    /* do nothing in this project */
    return 0;
}

// copy_thread - setup the trapframe on the  process's kernel stack top and
//             - 设置进程的内核入口点和栈
//proc：指向目标进程（子进程）结构体的指针
static void
copy_thread(struct proc_struct *proc, uintptr_t esp, struct trapframe *tf) {
    //内核栈的增长是向下的（从高地址向低地址），
    //所以 proc->kstack + KSTACKSIZE - sizeof(struct trapframe) 就是栈顶，即 trapframe 在内核栈中的位置
    proc->tf = (struct trapframe *)(proc->kstack + KSTACKSIZE - sizeof(struct trapframe));
    //将父进程的 trapframe 的内容(寄存器状态和上下文)复制到新进程的 trapframe 中
    *(proc->tf) = *tf;

    // Set a0 to 0 so a child process knows it's just forked
    //a0寄存器（返回值）设置为0，说明这个进程是一个子进程
    proc->tf->gpr.a0 = 0;
    //如果传入的 esp 为 0，表示没有特别指定栈指针地址，那么将栈指针设置为 proc->tf（即新进程的 trapframe 的位置）。这确保新进程从它的 trapframe 开始执行。
    //如果 esp 不为 0，则将 esp 作为新进程的堆栈指针。
    proc->tf->gpr.sp = (esp == 0) ? (uintptr_t)proc->tf : esp;

    proc->context.ra = (uintptr_t)forkret;

    //把trapframe放在上下文的栈顶
    proc->context.sp = (uintptr_t)(proc->tf);
}

/* do_fork -     parent process for a new child process
 * @clone_flags: used to guide how to clone the child process
 * @stack:       the parent's user stack pointer. if stack==0, It means to fork a kernel thread.
 * @tf:          the trapframe info, which will be copied to child process's proc->tf
 */
int
do_fork(uint32_t clone_flags, uintptr_t stack, struct trapframe *tf) {
    int ret = -E_NO_FREE_PROC;//错误码，表示没有可用的进程控制块
    struct proc_struct *proc;
    if (nr_process >= MAX_PROCESS) {//如果进程数超限，就跳转到 fork_out 标签执行退出逻辑
        goto fork_out;
    }
    ret = -E_NO_MEM;//表示内存分配失败
    //LAB4:EXERCISE2 YOUR CODE
    /*
     * Some Useful MACROs, Functions and DEFINEs, you can use them in below implementation.
     * MACROs or Functions:
     *   alloc_proc:   create a proc struct and init fields (lab4:exercise1)
     *   setup_kstack: alloc pages with size KSTACKPAGE as process kernel stack
     *   copy_mm:      process "proc" duplicate OR share process "current"'s mm according clone_flags
     *                 if clone_flags & CLONE_VM, then "share" ; else "duplicate"
     *   copy_thread:  setup the trapframe on the  process's kernel stack top and
     *                 setup the kernel entry point and stack of process
     *   hash_proc:    add proc into proc hash_list
     *   get_pid:      alloc a unique pid for process
     *   wakeup_proc:  set proc->state = PROC_RUNNABLE
     * VARIABLES:
     *   proc_list:    the process set's list
     *   nr_process:   the number of process set
     */

// 1 分配并初始化进程控制块（alloc_proc函数）
// 2 分配并初始化内核栈（setup_stack函数）
// 3 根据clone_flags决定是复制还是共享内存管理系统（copy_mm函数）
// 4 设置进程的中断帧和上下文（copy_thread函数）
// 5 把设置好的进程加入链表
// 6 将新建的进程设为就绪态
// 7 将返回值设为线程id
    if ((proc = alloc_proc()) == NULL) {//分配一个 initproc（
        goto fork_out;
    }
    proc->parent = current;//将子进程的父节点设置为当前进程

    if(setup_kstack(proc)){//将分配到的页面映射到进程的内核栈指针
        goto bad_fork_cleanup_proc;
    }

    if(copy_mm(clone_flags,proc)){//copy_mm函数目前只是把current->mm设置为NULL，这是由于目前在实验四中只能创建内核线程，proc->mm描述的是进程用户态空间的情况，所以目前mm还用不上。
        goto bad_fork_cleanup_kstack;
    }

    copy_thread(proc,stack,tf);

    bool intr_flag;
    //sync/sync.h
    local_intr_save(intr_flag);//屏蔽中断，intr_flag置为1   
    {
        proc->pid = get_pid();//获取当前进程PID
        hash_proc(proc);//建立hash映射
        list_add(&proc_list,&(proc->list_link));//加入进程链
        nr_process++;
    }
    local_intr_restore(intr_flag);//恢复中断

    proc->state = PROC_RUNNABLE;

    ret = proc->pid;//返回当前进程的PID
//返回标签
fork_out:
    return ret;

bad_fork_cleanup_kstack:
    put_kstack(proc);
bad_fork_cleanup_proc:
    kfree(proc);
    goto fork_out;
}

// do_exit - called by sys_exit
//   1. call exit_mmap & put_pgdir & mm_destroy to free the almost all memory space of process
//   2. set process' state as PROC_ZOMBIE, then call wakeup_proc(parent) to ask parent reclaim itself.
//   3. call scheduler to switch to other process
int
do_exit(int error_code) {
    panic("process exit!!.\n");
}

// init_main - the second kernel thread used to create user_main kernel threads
static int
init_main(void *arg) {
    cprintf("this initproc, pid = %d, name = \"%s\"\n", current->pid, get_proc_name(current));
    cprintf("To U: \"%s\".\n", (const char *)arg);
    cprintf("To U: \"en.., Bye, Bye. :)\"\n");
    return 0;
}

// proc_init - set up the first kernel thread idleproc "idle" by itself and 
//           - create the second kernel thread init_main
//完成了idleproc内核线程和initproc内核线程的创建或复制工作
void
proc_init(void) {
    int i;

    list_init(&proc_list);//存储系统中的进程
    for (i = 0; i < HASH_LIST_SIZE; i ++) {
        list_init(hash_list + i);
    }

    if ((idleproc = alloc_proc()) == NULL) {//分配一个 idleproc（空闲进程）
        panic("cannot alloc idleproc.\n");
    }

    // check the proc structure
    //分配一块内存，存储进程的上下文信息
    int *context_mem = (int*) kmalloc(sizeof(struct context));
    memset(context_mem, 0, sizeof(struct context));//内存清零，确保其内容为空
    //比较 idleproc 进程的 context 是否与清零后的内存一致
    int context_init_flag = memcmp(&(idleproc->context), context_mem, sizeof(struct context));

    //为进程名称分配内存
    int *proc_name_mem = (int*) kmalloc(PROC_NAME_LEN);
    memset(proc_name_mem, 0, PROC_NAME_LEN);
    int proc_name_flag = memcmp(&(idleproc->name), proc_name_mem, PROC_NAME_LEN);

    //对 idleproc 进程结构体进行一系列检查
    if(idleproc->cr3 == boot_cr3 && idleproc->tf == NULL && !context_init_flag
        && idleproc->state == PROC_UNINIT && idleproc->pid == -1 && idleproc->runs == 0
        && idleproc->kstack == 0 && idleproc->need_resched == 0 && idleproc->parent == NULL
        && idleproc->mm == NULL && idleproc->flags == 0 && !proc_name_flag
    ){
        cprintf("alloc_proc() correct!\n");

    }
    //初始化 idleproc 进程的状态
    idleproc->pid = 0;
    idleproc->state = PROC_RUNNABLE;//表示该进程可以被调度
    //uCore启动时设置的内核栈直接分配给idleproc使用
    idleproc->kstack = (uintptr_t)bootstack;//分配内核栈
    idleproc->need_resched = 1;//表示该进程需要调度
    set_proc_name(idleproc, "idle");
    nr_process ++;//系统中的进程总数

    current = idleproc;
    //创建initproc内核线程,显示“Hello World”，表明自己存在且能正常工作
    int pid = kernel_thread(init_main, "Hello world!!", 0);
    if (pid <= 0) {
        panic("create init_main failed.\n");
    }
    //使用 find_proc 查找新创建的 init_main 线程
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

