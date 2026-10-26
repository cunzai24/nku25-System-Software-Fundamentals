#ifndef _KERN_MM_BUDDY_PMM_H_
#define _KERN_MM_BUDDY_PMM_H_
// 头文件保护，防止重复包含

#include <pmm.h>
// 需要用到 struct pmm_manager 的定义（在 pmm.h 里）

// 对外暴露测试函数，供 pmm_init() 或 main 里调用
void buddy_test(void);

// 对外暴露 buddy 物理内存管理器实例
// 其他文件（如 pmm.c）通过 extern 声明后，可以 &buddy_pmm_manager 切换管理器
extern const struct pmm_manager buddy_pmm_manager;

#endif
// 结束头文件保护