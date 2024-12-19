#include <defs.h>
#include <unistd.h>
#include <stdarg.h>
#include <syscall.h>

#define MAX_ARGS            5

static inline int
syscall(int64_t num, ...) {
    //va_list, va_start, va_arg都是C语言处理参数个数不定的函数的宏
    //在stdarg.h里定义
    va_list ap;//ap: 参数列表(此时未初始化)
    va_start(ap, num);//初始化参数列表, 从num开始
    //First, va_start initializes the list of variable arguments as a va_list.
    uint64_t a[MAX_ARGS];// 用于存储参数的数组
    int i, ret;
    for (i = 0; i < MAX_ARGS; i ++) {//把参数依次取出
    /*Subsequent executions of va_arg yield the values of the additional arguments 
           in the same order as passed to the function.*/
        a[i] = va_arg(ap, uint64_t);// 从 ap 中依次提取参数存入数组 a
    }
    va_end(ap);// 清理参数列表ap，释放资源
    //Finally, va_end shall be executed before the function returns.

    asm volatile (
    "ld a0, %1\n"    // 将 num 加载到寄存器 a0
    "ld a1, %2\n"    // 将第一个参数加载到 a1
    "ld a2, %3\n"    // 将第二个参数加载到 a2
    "ld a3, %4\n"    // 将第三个参数加载到 a3
    "ld a4, %5\n"    // 将第四个参数加载到 a4
    "ld a5, %6\n"    // 将第五个参数加载到 a5
    "ecall\n"        // 触发系统调用
    "sd a0, %0"      // 将返回值从 a0 存入 ret
    : "=m" (ret)    // 输出：将结果存储到 ret
    : "m"(num), "m"(a[0]), "m"(a[1]), "m"(a[2]), "m"(a[3]), "m"(a[4])
    : "memory"       // 声明内存可能被修改，防止编译器优化
    );

    //num存到a0寄存器， a[0]存到a1寄存器
    //ecall的返回值存到ret
    return ret;
}

int
sys_exit(int64_t error_code) {
    return syscall(SYS_exit, error_code);
}

int
sys_fork(void) {
    return syscall(SYS_fork);
}

int
sys_wait(int64_t pid, int *store) {
    return syscall(SYS_wait, pid, store);
}

int
sys_yield(void) {
    return syscall(SYS_yield);
}

int
sys_kill(int64_t pid) {
    return syscall(SYS_kill, pid);
}

int
sys_getpid(void) {
    return syscall(SYS_getpid);
}

int
sys_putc(int64_t c) {
    return syscall(SYS_putc, c);
}

int
sys_pgdir(void) {
    return syscall(SYS_pgdir);
}

