#pragma once
#include "types.h"

#define PAGE_SIZE 4096

extern u64 hhdm_offset;

#define PHYS_TO_VIRT(addr) ((void *)((u64)(addr) + hhdm_offset))
#define VIRT_TO_PHYS(addr) ((u64)(addr) - hhdm_offset)

// Page table flags
#define PTE_PRESENT  (1UL << 0)
#define PTE_WRITE    (1UL << 1)
#define PTE_USER     (1UL << 2)
#define PTE_PWT      (1UL << 3)  // Write-through
#define PTE_PCD      (1UL << 4)  // Cache disable
#define PTE_NX       (1UL << 63) // No execute

// Page frame mask (clear lower 12 bits)
#define PAGE_FRAME_MASK  (~0xFFFUL)

// Memory patterns for debugging
#define MEM_FREE_PATTERN   1    // Pattern written to freed pages
#define MEM_ALLOC_PATTERN  5    // Pattern written to allocated pages

// Page table entry helpers
typedef u64 pte_t;

static inline u64 pte_get_phys(pte_t pte) {
    return pte & PAGE_FRAME_MASK;
}

static inline int pte_is_present(pte_t pte) {
    return (pte & PTE_PRESENT) != 0;
}

void freerange(u64 phys_start, u64 phys_end);
void kfree(void *v, u64 npages);
void *kalloc(u64 npages);
void *memset(void *dst, int c, u64 n);
void kinit(u64 hhdm);
void map_page(u64 virt, u64 phys, u64 flags);
void map_page_pml4(u64 *pml4, u64 virt, u64 phys, u64 flags);
u64 *create_user_pml4(void);
void copy_user_pml4(u64 *new_pml4, u64 *old_pml4);
void free_user_pml4(u64 *pml4);
void map_mmio(u64 phys, u64 size);
void *memcpy(void *dst, const void *src, u64 n);
void buddy_enable_lock(void);
