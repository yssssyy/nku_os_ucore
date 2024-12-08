#ifndef __KERN_SYNC_SYNC_H__
#define __KERN_SYNC_SYNC_H__

#include <defs.h>
#include <intr.h>
#include <riscv.h>

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
//禁用当前的中断，并将当前中断状态保存到变量 x 中。它使用 __intr_save() 函数来保存当前的中断状态。
#define local_intr_save(x) \
    do {                   \
        x = __intr_save(); \
    } while (0)
//恢复之前保存的中断状态
#define local_intr_restore(x) __intr_restore(x);

#endif /* !__KERN_SYNC_SYNC_H__ */
