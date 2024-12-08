## <center> Lab4实验报告</center>


### 练习1：分配并初始化一个进程控制块（需要编码）
alloc_proc函数（位于kern/process/proc.c中）负责分配并返回一个新的struct proc_struct结构，用于存储新建立的内核线程的管理信息。ucore需要对这个结构进行最基本的初始化，你需要完成这个初始化过程。
> 【提示】在alloc_proc函数的实现中，需要初始化的proc_struct结构中的成员变量至少包括：state/pid/runs/kstack/need_resched/parent/mm/context/tf/cr3/flags/name。

请在实验报告中简要说明你的设计实现过程。请回答如下问题：
* 请说明proc_struct中struct context context和struct trapframe *tf成员变量含义和在本实验中的作用是啥？（提示通过看代码和编程调试可以判断出来）

#### 设计实现：

我们要完成的alloc_proc 函数的主要作用是分配并初始化一个进程控制块（PCB），以便管理新进程的相关信息。进程控制块（PCB），proc_struct 结构体主要包含以下信息：

```c
struct proc_struct { // 进程控制块 (PCB)
    enum proc_state state;                      // 进程当前状态
    int pid;                                    // 进程ID
    int runs;                                   // 进程已运行时间（以某种单位计）
    uintptr_t kstack;                           // 内核栈的起始地址
    volatile bool need_resched;                 // 标记进程是否需要调度
    struct proc_struct *parent;                 // 指向父进程的指针
    struct mm_struct *mm;                       // 进程的虚拟内存管理信息
    struct context context;                     // 进程的上下文（寄存器等状态信息）
    struct trapframe *tf;                       // 当前中断帧的指针
    uintptr_t cr3;                              // 当前进程的页表基地址（x86架构）
    uint32_t flags;                             // 进程的标志位，用于表示一些进程状态或特性
    char name[PROC_NAME_LEN + 1];               // 进程的名称（包括字符串结束符）
    list_entry_t list_link;                     // 用于进程链表的链表节点
    list_entry_t hash_link;                     // 用于进程哈希表的链表节点
};
```

alloc_proc函数中我们需要结构体中的对每个变量都进行初始化操作：

```c
static struct proc_struct *
alloc_proc(void) {
    // 为新进程分配内存空间
    struct proc_struct *proc = kmalloc(sizeof(struct proc_struct));
    if (proc != NULL) {
        // 初始化进程状态为未初始化
        proc->state = PROC_UNINIT;      
        // 设置进程 ID 为 -1，表示该进程尚未初始化
        proc->pid = -1;     
        // 初始化运行时间为 0，表示刚创建的进程尚未运行
        proc->runs = 0;        
        // 内核栈的起始地址为 0，表示尚未分配栈空间
        proc->kstack = 0;        
        // 设置不需要调度
        proc->need_resched = 0;        
        // 初始化父进程指针为空，表示该进程尚未与其他进程建立父子关系
        proc->parent = NULL;       
        // 设置虚拟内存为 NULL，表示该进程尚未分配虚拟内存空间
        proc->mm = NULL;       
        // 将进程的上下文清零，确保初始化时没有残留的旧数据
        memset(&(proc->context), 0, sizeof(struct context));        
        // 中断帧指针初始化为 NULL，表示该进程尚未处理中断
        proc->tf = NULL;        
        // 设置页表基地址为内核页表基址，boot_cr3 是内核启动时设定的页目录基地址
        proc->cr3 = boot_cr3;        
        // 进程标志位初始化为 0，表示没有特殊标志
        proc->flags = 0;       
        // 进程名清零，表示尚未为进程分配名称
        memset(proc->name, 0, PROC_NAME_LEN);
    }    
    // 返回分配并初始化好的进程控制块
    return proc;
}
```

#### 问题回答：

- `struct context context` 是一个用于保存进程上下文的结构体，其中存储了与进程执行状态相关的寄存器值。

```c
struct context {
     uintptr_t ra;  // 返回地址
     uintptr_t sp;  // 栈指针
     uintptr_t s0;  // 以下均为保存寄存器
     uintptr_t s1;
     uintptr_t s2;
     uintptr_t s3;
     uintptr_t s4;
     uintptr_t s5;
     uintptr_t s6;
     uintptr_t s7;
     uintptr_t s8;
     uintptr_t s9;
     uintptr_t s10;
     uintptr_t s11;
 };
```

- `struct trapframe *tf` 是进程中断帧的指针，指向内核栈中的一个特定位置：
  - 当进程从用户态切换到内核态时，中断帧保存了进程在中断前的状态，包括部分寄存器的值。
  - 当**内核需要恢复到用户态**时，会根据中断帧的内容恢复相关寄存器的值，以便进程能够继续执行。

```C
struct trapframe {
    struct pushregs gpr;  // 通用寄存器
    uintptr_t status;  // 状态
    uintptr_t epc;  // pc值
    uintptr_t badvaddr;  // 发生错误的地址
    uintptr_t cause;  // 错误原因
};
```

### 练习2：为新创建的内核线程分配资源（需要编码）

创建一个内核线程需要分配和设置好很多资源。kernel_thread函数通过调用**do_fork**函数完成具体内核线程的创建工作。do_kernel函数会调用alloc_proc函数来分配并初始化一个进程控制块，但alloc_proc只是找到了一小块内存用以记录进程的必要信息，并没有实际分配这些资源。ucore一般通过do_fork实际创建新的内核线程。do_fork的作用是，创建当前内核线程的一个副本，它们的执行上下文、代码、数据都一样，但是存储位置不同。因此，我们**实际需要"fork"的东西就是stack和trapframe**。在这个过程中，需要给新内核线程分配资源，并且复制原进程的状态。你需要完成在kern/process/proc.c中的do_fork函数中的处理过程。它的大致执行步骤包括：
* 调用alloc_proc，首先获得一块用户信息块。
* 为进程分配一个内核栈。
* 复制原进程的内存管理信息到新进程（但内核线程不必做此事）
* 复制原进程上下文到新进程
* 将新进程添加到进程列表
* 唤醒新进程
* 返回新进程号
  

请在实验报告中简要说明你的设计实现过程。请回答如下问题：
* 请说明ucore是否做到给每个新fork的线程一个唯一的id？请说明你的分析和理由。
  

#### 设计实现：

根据代码中的提示，do_fork为创建新进程可以大致分为以下7步：

1. 调用alloc_proc函数分配进程结构体 
   
   ```c++
   proc = alloc_proc();                 // 分配新的进程结构体并初始化其字段
    if(proc==NULL)                       // 如果进程结构体分配失败
    {
        goto bad_fork_cleanup_proc;
    }
   ```
   调用alloc_proc()为新创建的进程分配一个进程结构体并进行初始化，如果申请成功，则继续执行，否则跳转到bad_fork_cleanup_proc释放结构体

2. 调用setup_kstack()为新进程分配内核
   
   ```c++
   if(setup_kstack(proc))               // 为新进程分配内核栈
    {
        goto bad_fork_cleanup_kstack;    // 如果内核栈分配失败，进入清理流程
    }  
   ```
   如果分配成功则继续执行，否则跳转到bad_fork_cleanup_kstack进行清理
3. 调用copy_mm去复制原进程的内存管理信息到新进程
   
   ```c++
   if(copy_mm(clone_flags, proc))       // 根据 clone_flags 复制或共享内存管理结构
    {
        goto fork_out;                   // 如果内存管理结构复制失败，跳到退出处理
    }
   ```
4. 调用copy_thread()函数来复制原进程上下文到新进程
   
   ```c++
   copy_thread(proc, stack, tf);    
   ```
5. 将新进程加入到进程链表和哈希链表中
   
   由于进程链表和哈希链表的操作都是依靠进行，因此首先使用get_pid()为新进程获取一个PID，然后调用hash_proc()和list_add()将新进程加入到哈希链表和进程链表中
   ```c++
    int pid = get_pid();                
    proc->pid = pid;                     // 将 PID 分配给新进程
    hash_proc(proc);                     // 将新进程插入哈希表，便于快速查找
    list_add(&proc_list, &(proc->list_link)); // 将新进程加入进程链表
   ```
6. 调用wakeup_proc()唤醒进程
   
   wakeup_proc()函数中将进程的状态设置为PROC_RUNNABLE
   ```c++
   wakeup_proc(proc);   
   ```
7. 返回新进程的PID
   
   ```c++
   ret = proc->pid;
   ```


#### 问题回答：

给每个新fork的线程一个唯一的id是通过调用get_pid()函数实现的

```c++
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
```

这段代码通过维护static int型的last_pid来实现唯一分配进程ID
* 每次分配一个新的 last_pid 时，会遍历当前所有进程的列表，检查是否有其他进程已经占用了这个 last_pid。
* 如果发现冲突，会递增 last_pid 并继续检查，直到找到一个未被占用的值。
* 如果 last_pid 超过了 MAX_PID，会从 1 重新开始分配，并重新遍历整个进程列表，确保分配的 PID 唯一。
  

通过这种方式，为每一个进程分配一个未被使用过的PID，实现唯一性

### 练习3：编写proc_run 函数（需要编码）

proc_run用于将指定的进程切换到CPU上运行。它的大致执行步骤包括：
* 检查要切换的进程是否与当前正在运行的进程相同，如果相同则不需要切换。
* 禁用中断。你可以使用/kern/sync/sync.h中定义好的宏local_intr_save(x)和local_intr_restore(x)来实现关、开中断。
* 切换当前进程为要运行的进程。
* 切换页表，以便使用新进程的地址空间。/libs/riscv.h中提供了lcr3(unsigned int cr3)函数，可实现修改CR3寄存器值的功能。
* 实现上下文切换。/kern/process中已经预先编写好了switch.S，其中定义了switch_to()函数。可实现两个进程的context切换。
* 允许中断。
  

请回答如下问题：
* 在本实验的执行过程中，创建且运行了几个内核线程？
  
#### 设计实现
根据手册里的提示，编写proc_run函数如下：
```c
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
        struct proc_struct *prev =current,*next = proc;
        local_intr_save(intr_flag);
        {
            current = proc;//让 current指向 next内核线程initproc
            lcr3(proc->cr3);//设置 CR3 寄存器的值为 next 内核线程 initproc 的页目录表起始地址 next->cr3，这实际上是完成进程间的页表切换
            switch_to(&(prev->context),&(next->context));//由 switch_to函数完成具体的两个线程的执行现场切换，即切换各个寄存器，当 switch_to 函数执行完“ret”指令后，就切换到initproc执行了。
        }
        local_intr_restore(intr_flag);
    }
}
```
函数的核心流程是：
保存中断状态 → 更新进程指针 → 切换页表 → 上下文切换 → 恢复中断状态

这里使用local_intr_save()屏蔽中断，完成操作后使用local_intr_restore()打开中断，以免进程切换时其他进程再进行调度，保护进程切换不会被中断。
#### 问题回答
在本实验中，创建且运行了2两个内核线程：

    - 0号线程idleproc：第一个内核进程，完成内核中各个子系统的初始化，之后立即调度，执行其他进程。
    - 1号线程initproc：用于完成实验的功能而调度的内核进程。

### 扩展练习Challenge:
* 说明语句local_intr_save(intr_flag);....local_intr_restore(intr_flag);是如何实现开关中断的？
  

这两个函数在kern/sync.h中定义：
```c++
static inline bool __intr_save(void) {
    if (read_csr(sstatus) & SSTATUS_SIE) {
        intr_disable();
        return 1;
    }
    return 0;
}

static inline void __intr_restore(bool flag) {
    if (flag) {
        intr_enable();
    }
}

#define local_intr_save(x) \
    do {                   \
        x = __intr_save(); \
    } while (0)
#define local_intr_restore(x) __intr_restore(x);
```

当调用local_intr_save()函数时，会跳转到__intr_save()函数中，读取sstatus寄存器，判断SIE位的值，如果是1，说明能进行中断，就会调用intr_disable将其置为0，返回1给x，intr_flag赋值为1；如果SIE位时0，则直接返回0，则说明中断此时已经不能进行，则返回0，将intr_flag赋值为0。以此保证之后的代码执行时不会发生中断。

当需要恢复中断时，调用local_intr_restore，需要判断intr_flag的值，如果其值为1，则需要调用intr_enable将sstatus寄存器的SIE位置1，否则该位依然保持0。以此来恢复调用local_intr_save之前的SIE的值。