#include <buddy_pmm.h>
#include <default_pmm.h>
#include <best_fit_pmm.h>
#include <defs.h>
#include <error.h>
#include <memlayout.h>
#include <mmu.h>
#include <pmm.h>
#include <sbi.h>
#include <stdio.h>
#include <string.h>
#include <riscv.h>
#include <dtb.h>

// virtual address of physical page array
struct Page *pages;
// amount of physical memory (in pages)
size_t npage = 0;
// the kernel image is mapped at VA=KERNBASE and PA=info.base
uint64_t va_pa_offset;
// memory starts at 0x80000000 in RISC-V
// DRAM_BASE defined in riscv.h as 0x80000000
const size_t nbase = DRAM_BASE / PGSIZE;

// virtual address of boot-time page directory
uintptr_t *satp_virtual = NULL;
// physical address of boot-time page directory
uintptr_t satp_physical;

// physical memory management
const struct pmm_manager *pmm_manager;


static void check_alloc_page(void);

// init_pmm_manager - initialize a pmm_manager instance
static void init_pmm_manager(void) {
    pmm_manager = &best_fit_pmm_manager;
    cprintf("memory management: %s\n", pmm_manager->name);
    pmm_manager->init();
}

// init_memmap - call pmm->init_memmap to build Page struct for free memory
static void init_memmap(struct Page *base, size_t n) {
    pmm_manager->init_memmap(base, n);
}

// alloc_pages - call pmm->alloc_pages to allocate a continuous n*PAGESIZE
// memory
struct Page *alloc_pages(size_t n) {
    return pmm_manager->alloc_pages(n);
}

// free_pages - call pmm->free_pages to free a continuous n*PAGESIZE memory
void free_pages(struct Page *base, size_t n) {
    pmm_manager->free_pages(base, n);
}

// nr_free_pages - call pmm->nr_free_pages to get the size (nr*PAGESIZE)
// of current free memory
size_t nr_free_pages(void) {
    return pmm_manager->nr_free_pages();
}

//lab2 function1 2511113
static void page_init(void) {
    extern char end[];

    /* The kernel image is mapped with this fixed virtual/physical offset. */
    va_pa_offset = PHYSICAL_MEMORY_OFFSET;

    uint64_t mem_base = get_memory_base();
    uint64_t mem_size = get_memory_size();

    /* Case 2: the device tree must describe a sane physical memory range. */
    if (mem_base == 0 || mem_size == 0 || mem_base + mem_size < mem_base) {
        panic("page_init: invalid physical memory range");
    }
    if (mem_base < DRAM_BASE) {
        panic("page_init: physical memory base 0x%016lx is below DRAM_BASE",
              mem_base);
    }

    /* Case 3: never manage memory beyond the limit of the memory layout. */
    uint64_t mem_end = mem_base + mem_size;
    if (mem_end > PHYSICAL_MEMORY_END) {
        mem_end = PHYSICAL_MEMORY_END;
    }
    if (mem_end <= mem_base) {
        panic("page_init: empty physical memory range");
    }

    /* Page descriptors are indexed by PPN(pa) - nbase. */
    npage = PPN(mem_end);
    if (npage <= nbase) {
        panic("page_init: no manageable physical pages");
    }

    cprintf("physical memory map:\n");
    cprintf("  memory: 0x%016lx, [0x%016lx, 0x%016lx].\n",
            mem_end - mem_base, mem_base, mem_end - 1);

    /* Put the Page descriptor array on the first page after the kernel. */
    uintptr_t pages_va = ROUNDUP((uintptr_t)end, PGSIZE);
    size_t managed_pages = npage - nbase;
    if (managed_pages > ((uintptr_t)-1 - pages_va) / sizeof(struct Page)) {
        panic("page_init: Page descriptor array overflow");
    }
    struct Page *descriptor_end =
        (struct Page *)(pages_va + managed_pages * sizeof(struct Page));
    pages = (struct Page *)pages_va;

    /* The descriptor array itself must fit in the described memory. */
    uintptr_t descriptors_pa = PADDR((uintptr_t)descriptor_end);
    if (descriptors_pa > mem_end) {
        panic("page_init: Page descriptors exceed physical memory");
    }

    /* Start with every descriptor reserved; init_memmap() frees the rest. */
    for (struct Page *p = pages; p < descriptor_end; p++) {
        p->ref = 0;
        p->flags = 0;
        p->property = 0;
        list_init(&(p->page_link));
        SetPageReserved(p);
    }

    /* Case 4: only complete pages inside the memory range can be freed. */
    uintptr_t free_start = ROUNDUP(descriptors_pa, PGSIZE);
    uintptr_t free_end = ROUNDDOWN(mem_end, PGSIZE);
    uintptr_t mem_start = ROUNDUP(mem_base, PGSIZE);
    if (free_start < mem_start) {
        free_start = mem_start;
    }

    /* Case 5: do not register a zero-sized or partial free range. */
    if (free_start < free_end) {
        init_memmap(pa2page(free_start), (free_end - free_start) / PGSIZE);
    }
}

/* pmm_init - initialize the physical memory management */
void pmm_init(void) {
    // We need to alloc/free the physical memory (granularity is 4KB or other size).
    // So a framework of physical memory manager (struct pmm_manager)is defined in pmm.h
    // First we should init a physical memory manager(pmm) based on the framework.
    // Then pmm can alloc/free the physical memory.
    // Now the first_fit/best_fit/worst_fit/buddy_system pmm are available.
    init_pmm_manager();

    // detect physical memory space, reserve already used memory,
    // then use pmm->init_memmap to create free page list
    page_init();

    // use pmm->check to verify the correctness of the alloc/free function in a pmm
    check_alloc_page();
    //只修改一行就可以切换
    if (pmm_manager == &buddy_pmm_manager) {
    buddy_test();
    }

    extern char boot_page_table_sv39[];
    satp_virtual = (pte_t*)boot_page_table_sv39;
    satp_physical = PADDR(satp_virtual);
    cprintf("satp virtual address: 0x%016lx\nsatp physical address: 0x%016lx\n", satp_virtual, satp_physical);
}

static void check_alloc_page(void) {
    pmm_manager->check();
    cprintf("check_alloc_page() succeeded!\n");
}
