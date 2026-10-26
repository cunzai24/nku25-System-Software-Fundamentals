#include <buddy_pmm.h>
#include <defs.h>
#include <list.h>
#include <memlayout.h>
#include <pmm.h>
#include <assert.h>
#include <stdio.h>

/*
 * ============================================================================
 * Buddy System 物理内存分配器 —— 在整个 pmm 流程中的位置
 * ============================================================================
 * 内核启动后：kern_init() -> pmm_init()
 *   pmm_init():
 *     1. init_pmm_manager()   -> 选中 buddy_pmm_manager，调用 buddy_init()
 *     2. page_init()          -> 探测物理内存、建 Page 描述符、算出空闲区，
 *                                然后调用 init_memmap(base,n)
 *                                -> 转发到 buddy_init_memmap(base,n)
 *     3. check_alloc_page()   -> 调用 pmm_manager->check
 *                                -> 转发到 buddy_check()
 *   之后任意时刻：alloc_pages(n) / free_pages(base,n)
 *     分别转发到 buddy_alloc_pages / buddy_free_pages
 *
 * Buddy 核心思想：
 *   - 空闲内存按 2^order 页（order=0..MAX_ORDER）分块管理；
 *   - 每个阶维护一条空闲块链表，链表节点挂在“块首页”的 page_link 上；
 *   - 分配：向上取整到 2^order，找最小可用的更大块，不断二分，后半块挂回低阶；
 *   - 释放：与“伙伴”（物理页号异或 2^order）合并，能合就升阶，递归向上。
 * ============================================================================
 */

#define MAX_ORDER 10   // 最大阶：1<<10 = 1024 页 = 4MB；阶数范围 0..MAX_ORDER

 /* 每个阶的空闲区：链表头 + 该阶空闲页总数 */
typedef struct {
    list_entry_t free_list;   // 该阶所有空闲块的链表头；节点是块首页的 page_link
    size_t nr_free;           // 该阶所有空闲块的页数之和（用于 nr_free_pages 统计）
} buddy_free_area_t;

/* 全局数组：buddy_free_area[order] 管理所有大小为 2^order 页的空闲块 */
static buddy_free_area_t buddy_free_area[MAX_ORDER + 1];

/*
 * 本管理器负责的连续物理页范围 [buddy_base_page, buddy_base_page+buddy_total_pages)
 * 作用：page_is_buddy() 判断“伙伴”是否越界；避免异或算出的 buddy 落在管理区外。
 * 注意：page_init 可能多次调用 init_memmap，所以用 min/max 扩展范围。
 */
static struct Page* buddy_base_page = NULL;
static size_t buddy_total_pages = 0;

/*
 * 把 struct Page* 转成“真正的物理页号”（PPN）。
 * 为什么不用 page - pages？
 *   pages 数组下标 0 对应物理页号 nbase（0x80000），下标和 PPN 差一个 nbase。
 *   Buddy 的伙伴关系依赖 PPN 的二进制位异或，必须用真实 PPN。
 */
static inline size_t page2idx(struct Page* page) {
    return page2ppn(page);   // page2ppn(page) = page - pages + nbase
}

/*
 * 判断 buddy 是否是 page 在 order 阶下的“合法空闲伙伴”。
 * 三个条件：
 *   1) buddy 落在本管理器管理范围内（防止异或越界到别的内存区）；
 *   2) buddy 是空闲块首页（PG_property = 1）；
 *   3) buddy 的块大小正好是 2^order（property 记录块页数）。
 * 只有三个条件都满足才能合并。
 */
static inline int page_is_buddy(struct Page* buddy, struct Page* page, int order) {
    if (buddy < buddy_base_page || buddy >= buddy_base_page + buddy_total_pages) {
        return 0;   // 伙伴越界，不能合并
    }
    if (PageProperty(buddy) && buddy->property == (1U << order)) {
        return 1;   // 伙伴空闲且大小匹配
    }
    return 0;
}

/*
 * 把一个大小为 2^order 页的块挂到对应阶的空闲链表。
 * base 必须是块首页。
 * 设置 property = 2^order，供 page_is_buddy 判断伙伴大小；
 * SetPageProperty 标记“这是空闲块首页”；
 * 该阶 nr_free 增加 2^order。
 */
static void add_block_to_order(struct Page* base, int order) {
    base->property = (1U << order);
    SetPageProperty(base);
    list_add(&(buddy_free_area[order].free_list), &(base->page_link));
    buddy_free_area[order].nr_free += (1U << order);
}

/*
 * 从对应阶空闲链表删除一个块（分配或合并时用）。
 * 清 PG_property，表示它不再是空闲块首页。
 */
static void del_block_from_order(struct Page* base, int order) {
    list_del(&(base->page_link));
    buddy_free_area[order].nr_free -= (1U << order);
    ClearPageProperty(base);
}

/*
 * 计算 base 在 order 阶下的“伙伴”页。
 * 伙伴 PPN = 当前 PPN 异或 (1 << order)。
 * 再减 nbase 转回 pages 数组下标，得到 struct Page*。
 * 例：order=0 时，伙伴就是相邻页；order=1 时，伙伴是相距 2 页的块首页。
 */
static struct Page* get_buddy(struct Page* base, int order) {
    size_t ppn = page2ppn(base);
    size_t buddy_ppn = ppn ^ (1UL << order);
    return pages + (buddy_ppn - nbase);
}

/*
 * buddy_init：pmm_init -> init_pmm_manager 时调用。
 * 初始化内部数据结构：清空所有阶链表和计数，重置管理范围。
 * 此时还没有空闲页，空闲页由后面的 page_init -> init_memmap 交进来。
 */
static void buddy_init(void) {
    int i;
    for (i = 0; i <= MAX_ORDER; i++) {
        list_init(&(buddy_free_area[i].free_list));
        buddy_free_area[i].nr_free = 0;
    }
    buddy_base_page = NULL;
    buddy_total_pages = 0;
}

/*
 * buddy_init_memmap：pmm_init -> page_init 之后，由框架 init_memmap 转发调用。
 * 入参 base/n 是 page_init 算出来的“完整空闲页区间”的首页和页数。
 * 步骤：
 *   1) 初始化这 n 页：清 flags/property，引用计数置 0；
 *      page_init 交来的页初始带 PG_reserved，这里清掉表示可用。
 *   2) 扩展管理范围（支持 page_init 多次调用）。
 *   3) 按 Buddy 规则把连续 n 页拆成若干 2^order 块，挂到对应阶链表：
 *      每次从 offset 开始，找“起始 PPN 对齐到 2^order 且不越界”的最大 order。
 */
static void buddy_init_memmap(struct Page* base, size_t n) {
    assert(n > 0);

    // 1) 初始化页面状态
    struct Page* p = base;
    size_t i;
    for (i = 0; i < n; i++, p++) {
        assert(PageReserved(p));        // 框架保证交来的是 reserved
        p->flags = 0;                   // 清 reserved/property
        p->property = 0;
        set_page_ref(p, 0);
        ClearPageProperty(p);
    }

    // 2) 扩展管理范围：取 min(起始) 和 max(结束)，支持多次 init_memmap
    if (buddy_base_page == NULL) {
        buddy_base_page = base;
        buddy_total_pages = n;
    }
    else {
        struct Page* end_old = buddy_base_page + buddy_total_pages;
        struct Page* end_new = base + n;
        if (base < buddy_base_page) {
            buddy_base_page = base;
        }
        if (end_new > end_old) {
            end_old = end_new;
        }
        buddy_total_pages = end_old - buddy_base_page;
    }

    // 3) 拆分并挂链
    size_t offset = 0;
    while (offset < n) {
        int order = MAX_ORDER;
        while (order >= 0) {
            size_t block_pages = (size_t)1 << order;
            size_t idx = page2idx(base + offset);
            // 起始 PPN 必须对齐到块大小，且剩余页数足够
            if ((idx & (block_pages - 1)) == 0 && offset + block_pages <= n) {
                break;
            }
            order--;
        }
        if (order < 0) {
            // 理论上不会发生：order=0 时 block_pages=1，总是对齐且 offset+1<=n
            panic("buddy_init_memmap: cannot split block\n");
        }
        add_block_to_order(base + offset, order);
        offset += ((size_t)1 << order);
    }
}

/*
 * buddy_alloc_pages：alloc_pages(n) -> pmm_manager->alloc_pages 转发到这里。
 * 分配至少 n 个连续页（实际分配 2^ceil(log2(n)) 页）。
 * 步骤：
 *   1) n 向上取整到 2^order；超过 MAX_ORDER 直接失败。
 *   2) 从 order 到 MAX_ORDER 找第一个非空链表。
 *   3) 取出该块，从链表删除。
 *   4) 若块比目标大，不断二分：后半块挂回低一阶，前半块继续。
 *   5) 返回首页，清 property，引用计数置 1。
 */
static struct Page* buddy_alloc_pages(size_t n) {
    if (n == 0 || n > ((size_t)1 << MAX_ORDER)) {
        return NULL;   // 非法或超过最大可分配块
    }

    // 1) 向上取整到 2 的幂
    size_t need = 1;
    int order = 0;
    while (need < n) {
        need <<= 1;
        order++;
    }

    // 2) 找第一个非空的、足够大的阶
    int cur = order;
    while (cur <= MAX_ORDER && list_empty(&(buddy_free_area[cur].free_list))) {
        cur++;
    }
    if (cur > MAX_ORDER) {
        return NULL;   // 没有足够大的空闲块
    }

    // 3) 取出该阶第一个块
    list_entry_t* le = list_next(&(buddy_free_area[cur].free_list));
    struct Page* page = le2page(le, page_link);
    del_block_from_order(page, cur);

    // 4) 二分：后半块挂回 cur-1 阶，前半块继续作为候选
    while (cur > order) {
        cur--;
        struct Page* buddy = page + ((size_t)1 << cur);
        add_block_to_order(buddy, cur);
    }

    // 5) 返回分配块首页
    page->property = 0;
    ClearPageProperty(page);
    set_page_ref(page, 1);
    return page;
}

/*
 * buddy_free_pages：free_pages(base,n) -> pmm_manager->free_pages 转发到这里。
 * 释放从 base 开始的 n 页（按 2^ceil(log2(n)) 处理），并尝试与伙伴合并。
 * 步骤：
 *   1) n 向上取整到 2^order。
 *   2) 清理 need 页状态；断言不能是 reserved，也不能已是空闲块首页（防重复释放）。
 *   3) 循环看当前阶伙伴是否空闲且同阶：是则从链表删伙伴、合并、order++。
 *   4) 把合并后的块挂回对应阶。
 */
static void buddy_free_pages(struct Page* base, size_t n) {
    assert(n > 0);
    assert(n <= ((size_t)1 << MAX_ORDER));

    // 1) 向上取整到 2 的幂
    size_t need = 1;
    int order = 0;
    while (need < n) {
        need <<= 1;
        order++;
    }

    // 2) 清理页面状态
    struct Page* p = base;
    size_t i;
    for (i = 0; i < need; i++, p++) {
        assert(!PageReserved(p));
        assert(!PageProperty(p));   // 防止重复释放/释放未分配页
        p->flags = 0;
        p->property = 0;
        set_page_ref(p, 0);
        ClearPageProperty(p);
    }

    // 3) 与伙伴合并，能合就升阶
    struct Page* page = base;
    int cur_order = order;
    while (cur_order < MAX_ORDER) {
        struct Page* buddy = get_buddy(page, cur_order);
        if (!page_is_buddy(buddy, page, cur_order)) {
            break;   // 伙伴不空闲或越界，停止合并
        }
        del_block_from_order(buddy, cur_order);  // 伙伴从空闲链表删除
        if (buddy < page) {
            page = buddy;   // 合并后取地址较小的作为新首页
        }
        cur_order++;
    }

    // 4) 挂回合并后的块
    add_block_to_order(page, cur_order);
}

/*
 * buddy_nr_free_pages：nr_free_pages() -> pmm_manager->nr_free_pages 转发到这里。
 * 返回当前所有阶空闲页总数。
 * 注意：这个值要和框架的 nr_free_pages() 语义一致（页数，不是块数）。
 */
static size_t buddy_nr_free_pages(void) {
    size_t total = 0;
    int i;
    for (i = 0; i <= MAX_ORDER; i++) {
        total += buddy_free_area[i].nr_free;
    }
    return total;
}

/*
 * buddy_check：pmm_init -> check_alloc_page 时，pmm_manager->check 转发到这里。
 * 做最小分配/释放/计数自检，保证基本正确性。
 * check 做了什么？分配 1 页、分配 3 页（按 4 页），
 * 检查 nr_free_pages 增减正确，释放后恢复，并打印 PASS。
 */
static void buddy_check(void) {
    size_t before = buddy_nr_free_pages();

    struct Page* p = buddy_alloc_pages(1);
    assert(p != NULL);
    assert(buddy_nr_free_pages() == before - 1);
    buddy_free_pages(p, 1);
    assert(buddy_nr_free_pages() == before);

    p = buddy_alloc_pages(3);   // 按 4 页算
    assert(p != NULL);
    assert(buddy_nr_free_pages() == before - 4);
    buddy_free_pages(p, 3);
    assert(buddy_nr_free_pages() == before);

    cprintf("buddy_check: PASS\n");
}

/*
 * buddy_test：自定义测试入口，可在 pmm_init() 里 page_init() 之后调用。
 * 覆盖：单页分配、多页取整、释放恢复、超限请求失败。
 * 并用 nr_free_pages 守恒验证。
 */
void buddy_test(void) {
    size_t before = buddy_nr_free_pages();
    cprintf("buddy_test: before free pages = %d\n", before);

    struct Page* p1 = buddy_alloc_pages(1);
    assert(p1 != NULL);
    assert(buddy_nr_free_pages() == before - 1);

    struct Page* p2 = buddy_alloc_pages(3);   // 按 4 页
    assert(p2 != NULL);
    assert(buddy_nr_free_pages() == before - 1 - 4);

    buddy_free_pages(p1, 1);
    buddy_free_pages(p2, 3);
    assert(buddy_nr_free_pages() == before);

    // 超过最大阶的请求应失败
    struct Page* p3 = buddy_alloc_pages((1 << (MAX_ORDER + 1)));
    assert(p3 == NULL);

    cprintf("buddy_test: ALL PASS\n");
}

//注册 buddy 物理内存管理器。
const struct pmm_manager buddy_pmm_manager = {
    .name = "buddy_pmm_manager",
    .init = buddy_init,
    .init_memmap = buddy_init_memmap,
    .alloc_pages = buddy_alloc_pages,
    .free_pages = buddy_free_pages,
    .nr_free_pages = buddy_nr_free_pages,
    .check = buddy_check,
};