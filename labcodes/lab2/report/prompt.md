# lab2 AI 提示词汇总

## 使用信息

| 成员 | 模块 | AI工具 | 模型 |
|---|---|---|---|
| 2511113-侯悦 | `page_init()`与整合测试 | Claude Code for VS Code | DeepSeek-V4.1-Flash |
| 2512256-付佳 | First-Fit分析与Best-Fit | Codex | GPT-6.1-sol |
| 2512322-丁心怡 | Buddy System与调试 | Claude Code（DeepSeek API） | DeepSeek-V4.1-Flash |

---

## 2511113-侯悦：物理内存探测与Page描述符初始化

````markdown
[PROMPT]
**任务**：修改 `kern/mm/pmm.c` 中的 `page_init()`，完成物理内存探测、Page描述符数组布局、保留页初始化和剩余空闲页注册。
**操作要求**：必须修改项目实际文件；保留已有代码和责任标记，仅补充 `//lab2 function1 2511113` 对应功能，不修改分配算法和其他成员模块。
**输出要求**：使用工程真实宏、全局变量和辅助函数，不引入无关接口；完成后说明关键状态和测试结果。

[RELY]
- 设备树接口：`get_memory_base()`、`get_memory_size()`。
- 地址与页宏：`KERNBASE`、`PHYSICAL_MEMORY_OFFSET`、`PHYSICAL_MEMORY_END`、`PGSIZE`、`ROUNDUP`、`ROUNDDOWN`、`PADDR`、`KADDR`、`PPN`。
- 全局状态：`va_pa_offset`、`npage`、`pages`、内核结束符号 `end`。
- Page接口：`SetPageReserved`、`set_page_ref`、`init_memmap`、`panic`、`cprintf`。

[GUARANTEE]
必须实现：
```c
static void page_init(void);
```
不得覆盖 `pmm_manager` 选择、Best-Fit、Buddy或评分文件。

[SPECIFICATION]
## page_init
**Pre-Condition**：设备树已经初始化，能够提供内存基址和大小。
**Post-Condition**：`va_pa_offset`、`npage`、`pages`和空闲页范围处于一致状态。
**Case 1**：设备树信息有效时计算 `[mem_base, mem_end)`，并将上界限制到 `PHYSICAL_MEMORY_END`。
**Case 2**：设备树信息无效、范围回绕或不存在完整空闲页时，安全终止或不调用 `init_memmap()`。
**Case 3**：把 `pages` 放在内核镜像结尾之后的页对齐位置，并初始化所有Page描述符为保留态。
**Case 4**：排除内核、固件及Page描述符数组后，仅把页对齐的完整空闲区交给 `init_memmap()`。
**Requirements**：保持项目既有输出格式，不改变其他模块公开接口。
````

---

## 2512256-付佳：First-Fit分析与Best-Fit实现

````markdown
[PROMPT]
**任务**：基于初始化同学完成的Lab 2工程，分析First-Fit并实现 `kern/mm/best_fit_pmm.c` 的Best-Fit物理页分配模块。
**操作要求**：仅修改Best-Fit责任区域和管理器接入点；保留侯悦完成的 `page_init()`、责任标记和框架测试；不实现Buddy任务。
**输出要求**：直接修改实际文件，以工程定义为准，并说明First-Fit与Best-Fit差异、状态不变量和真实测试结果。

[RELY]
- `pmm.h` 中的 `struct pmm_manager` 与 `free_area_t`。
- `memlayout.h` 中的 `struct Page` 和Page标志操作。
- `list.h` 中的双向链表接口与 `le2page`。
- `default_pmm.c` 的First-Fit实现以及原有 `basic_check`、`best_fit_check`。

[GUARANTEE]
必须实现：
```c
static void best_fit_init(void);
static void best_fit_init_memmap(struct Page *base, size_t n);
static struct Page *best_fit_alloc_pages(size_t n);
static void best_fit_free_pages(struct Page *base, size_t n);
static size_t best_fit_nr_free_pages(void);
const struct pmm_manager best_fit_pmm_manager;
```
辅助函数必须为 `static`，不得修改评分文件或其他成员实现。

[SPECIFICATION]
## best_fit_alloc_pages
**Pre-Condition**：`n > 0`，空闲链表有效。
**Post-Condition**：从所有满足请求的块中选择最小块；必要时切分并登记剩余部分；`nr_free`准确减少。
**Case 1**：存在精确大小块时直接选取。
**Case 2**：不存在精确块时选取所有可用块中最小的一块。
**Case 3**：无足够大的连续块时返回 `NULL`。

## best_fit_free_pages
**Pre-Condition**：释放区间合法且当前处于已分配状态。
**Post-Condition**：释放块进入按地址排序的空闲链表，并与连续的前后块合并。
**Requirements**：保持 `property`、`PageProperty`、链表和 `nr_free` 一致，并通过原有测试。
````

---

## 2512322-丁心怡：Buddy System实现

````markdown
[PROMPT]
**任务**：在 `kern/mm/` 新增 `buddy_pmm.c` 和 `buddy_pmm.h`，实现Buddy System物理页分配模块并接入 `pmm_manager` 框架。
**操作要求**：必须修改实际文件；保留现有初始化与Best-Fit代码；新增完整的分配、释放、分裂、合并与统计逻辑。
**输出要求**：使用工程的真实数据结构和接口，遵循以下行为规格。

[RELY]
- `PGSIZE`、`PG_reserved`、`PG_property`。
- `struct Page` 的 `ref`、`flags`、`property`、`page_link`。
- 双向链表接口、`le2page`、`page2ppn`、`set_page_ref`、`panic`。
- Page数组下标与真实PPN之间存在 `nbase` 偏移。

[GUARANTEE]
必须实现 `buddy_init`、`buddy_init_memmap`、`buddy_alloc_pages`、`buddy_free_pages`、`buddy_nr_free_pages`、`buddy_check` 和 `buddy_pmm_manager`。辅助函数必须为 `static`。

[SPECIFICATION]
## buddy_init_memmap
将空闲区按地址对齐和剩余长度拆为尽可能大的 `2^order` 块，并加入对应阶链表。
## buddy_alloc_pages
请求向上取整到2的幂；寻找对应阶或更高阶块；高阶块逐级二分，后半块放回低阶链表；超过 `1 << MAX_ORDER` 或无可用块时返回 `NULL`。
## buddy_free_pages
清理页面状态，通过 `PPN ^ (1 << order)` 计算伙伴；伙伴必须合法、空闲且同阶才能合并；不断向上合并后放回最终阶链表。
## buddy_nr_free_pages
返回所有阶空闲页数之和，而不是块数。
````

## 2512322-丁心怡：Buddy测试

````markdown
[PROMPT]
**任务**：修改 `kern/mm/buddy_pmm.c`，实现基础自检 `buddy_check` 和扩展测试 `buddy_test`。
**操作要求**：使用工程提供的 `assert` 和 `cprintf`，并保证测试结束后系统状态恢复。
**输出要求**：覆盖单页、多页取整、释放恢复和超限请求，输出明确PASS信息。

[RELY]
- `assert()`、`cprintf()`、`struct Page`。
- `buddy_alloc_pages`、`buddy_free_pages`、`buddy_nr_free_pages`。

[GUARANTEE]
```c
static void buddy_check(void);
void buddy_test(void);
```

[SPECIFICATION]
## buddy_check
记录初始空闲页数；测试申请1页和申请3页（实际占4页）；释放后空闲页数恢复。
## buddy_test
验证单页与多页分配、2的幂取整、释放后的计数守恒，以及超过 `1 << MAX_ORDER` 时返回 `NULL`；使用 `%lu`和显式类型转换打印 `size_t`。
````

