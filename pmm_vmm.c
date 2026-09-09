#include "pmm_vmm.h"

#define MAX_PHYS_MEM (16ULL * 1024ULL * 1024ULL * 1024ULL) // Bis zu 16 GB RAM Support
#define BITMAP_SIZE  (MAX_PHYS_MEM / PAGE_SIZE / 8ULL)

static UINT8 g_pmm_bitmap[BITMAP_SIZE] __attribute__((aligned(4096)));
static UINT64 g_total_frames = 0;
static UINT64 g_free_frames = 0;
static UINT64 g_kernel_cr3 = 0;

static inline void bitmap_set(UINT64 frame) {
    g_pmm_bitmap[frame / 8] |= (1 << (frame % 8));
}

static inline void bitmap_clear(UINT64 frame) {
    g_pmm_bitmap[frame / 8] &= ~(1 << (frame % 8));
}

static inline int bitmap_test(UINT64 frame) {
    return (g_pmm_bitmap[frame / 8] & (1 << (frame % 8))) != 0;
}

void pmm_init(EFI_MEMORY_DESCRIPTOR *mmap, UINTN map_size, UINTN desc_size) {
    __builtin_memset(g_pmm_bitmap, 0xFF, sizeof(g_pmm_bitmap)); // Anfangs alles als belegt markieren

    UINT64 cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    g_kernel_cr3 = cr3 & 0x000FFFFFFFFFF000ULL;

    UINTN entries = map_size / desc_size;
    g_free_frames = 0;
    g_total_frames = 0;

    for (UINTN i = 0; i < entries; i++) {
        EFI_MEMORY_DESCRIPTOR *desc = (EFI_MEMORY_DESCRIPTOR*)((UINT8*)mmap + i * desc_size);
        if (desc->Type == EfiConventionalMemory) {
            UINT64 start_frame = desc->PhysicalStart / PAGE_SIZE;
            UINT64 count = desc->NumberOfPages;

            for (UINT64 f = 0; f < count; f++) {
                UINT64 frame = start_frame + f;
                if (frame * PAGE_SIZE < MAX_PHYS_MEM) {
                    bitmap_clear(frame);
                    g_free_frames++;
                    g_total_frames++;
                }
            }
        }
    }

    // Untere 16 MB für Hardware/DMA/Kernel schützen
    for (UINT64 f = 0; f < 4096; f++) {
        if (!bitmap_test(f)) {
            bitmap_set(f);
            if (g_free_frames > 0) g_free_frames--;
        }
    }
}

void* pmm_alloc_frame(void) {
    for (UINT64 f = 4096; f < g_total_frames + 4096; f++) {
        if (!bitmap_test(f)) {
            bitmap_set(f);
            if (g_free_frames > 0) g_free_frames--;
            void *ptr = (void*)(UINTN)(f * PAGE_SIZE);
            __builtin_memset(ptr, 0, PAGE_SIZE);
            return ptr;
        }
    }
    return NULL; // Out of Memory!
}

void pmm_free_frame(void *phys_addr) {
    UINT64 frame = ((UINT64)(UINTN)phys_addr) / PAGE_SIZE;
    if (frame >= 4096 && frame < MAX_PHYS_MEM / PAGE_SIZE) {
        if (bitmap_test(frame)) {
            bitmap_clear(frame);
            g_free_frames++;
        }
    }
}

UINT64 pmm_get_free_memory_bytes(void) {
    return g_free_frames * PAGE_SIZE;
}

// Erstellt einen isolierten Adressraum mit Kernel-Spiegelung in den oberen 2 GB
UINT64* vmm_create_address_space(void) {
    UINT64 *pml4 = (UINT64*)pmm_alloc_frame();
    if (!pml4) return NULL;

    UINT64 *k_pml4 = (UINT64*)(UINTN)g_kernel_cr3;
    
    // Die oberen 256 Einträge (Kernel-Space / Hardware / Framebuffer) 1:1 spiegeln
    for (int i = 256; i < 512; i++) {
        pml4[i] = k_pml4[i];
    }
    
    // Identity-Map der unteren 1 GB für reibungslose Ring-0 Syscalls
    pml4[0] = k_pml4[0];

    return pml4;
}

int vmm_map_page(UINT64 *pml4, UINT64 virt_addr, UINT64 phys_addr, UINT64 flags) {
    UINT64 pml4_idx = (virt_addr >> 39) & 0x1FF;
    UINT64 pdpt_idx = (virt_addr >> 30) & 0x1FF;
    UINT64 pd_idx   = (virt_addr >> 21) & 0x1FF;
    UINT64 pt_idx   = (virt_addr >> 12) & 0x1FF;

    if (!(pml4[pml4_idx] & PAGE_PRESENT)) {
        void *frame = pmm_alloc_frame();
        if (!frame) return 0;
        pml4[pml4_idx] = ((UINT64)(UINTN)frame) | PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER;
    }
    UINT64 *pdpt = (UINT64*)(UINTN)(pml4[pml4_idx] & 0x000FFFFFFFFFF000ULL);

    if (!(pdpt[pdpt_idx] & PAGE_PRESENT)) {
        void *frame = pmm_alloc_frame();
        if (!frame) return 0;
        pdpt[pdpt_idx] = ((UINT64)(UINTN)frame) | PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER;
    }
    UINT64 *pd = (UINT64*)(UINTN)(pdpt[pdpt_idx] & 0x000FFFFFFFFFF000ULL);

    if (!(pd[pd_idx] & PAGE_PRESENT)) {
        void *frame = pmm_alloc_frame();
        if (!frame) return 0;
        pd[pd_idx] = ((UINT64)(UINTN)frame) | PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER;
    }
    UINT64 *pt = (UINT64*)(UINTN)(pd[pd_idx] & 0x000FFFFFFFFFF000ULL);

    pt[pt_idx] = (phys_addr & 0x000FFFFFFFFFF000ULL) | flags | PAGE_PRESENT;
    
    // TLB für diese Page invalidieren
    __asm__ volatile("invlpg (%0)" : : "r"(virt_addr) : "memory");
    return 1;
}

void vmm_unmap_page(UINT64 *pml4, UINT64 virt_addr) {
    UINT64 pml4_idx = (virt_addr >> 39) & 0x1FF;
    UINT64 pdpt_idx = (virt_addr >> 30) & 0x1FF;
    UINT64 pd_idx   = (virt_addr >> 21) & 0x1FF;
    UINT64 pt_idx   = (virt_addr >> 12) & 0x1FF;

    if (!(pml4[pml4_idx] & PAGE_PRESENT)) return;
    UINT64 *pdpt = (UINT64*)(UINTN)(pml4[pml4_idx] & 0x000FFFFFFFFFF000ULL);

    if (!(pdpt[pdpt_idx] & PAGE_PRESENT)) return;
    UINT64 *pd = (UINT64*)(UINTN)(pdpt[pdpt_idx] & 0x000FFFFFFFFFF000ULL);

    if (!(pd[pd_idx] & PAGE_PRESENT)) return;
    UINT64 *pt = (UINT64*)(UINTN)(pd[pd_idx] & 0x000FFFFFFFFFF000ULL);

    if (pt[pt_idx] & PAGE_PRESENT) {
        void *phys = (void*)(UINTN)(pt[pt_idx] & 0x000FFFFFFFFFF000ULL);
        pmm_free_frame(phys);
        pt[pt_idx] = 0;
        __asm__ volatile("invlpg (%0)" : : "r"(virt_addr) : "memory");
    }
}

void vmm_destroy_address_space(UINT64 *pml4) {
    if (!pml4) return;

    // Nur Userland-Tabellen freigeben (Einträge 0 bis 255)
    for (int pml4_i = 1; pml4_i < 256; pml4_i++) {
        if (pml4[pml4_i] & PAGE_PRESENT) {
            UINT64 *pdpt = (UINT64*)(UINTN)(pml4[pml4_i] & 0x000FFFFFFFFFF000ULL);
            for (int pdpt_i = 0; pdpt_i < 512; pdpt_i++) {
                if (pdpt[pdpt_i] & PAGE_PRESENT) {
                    UINT64 *pd = (UINT64*)(UINTN)(pdpt[pdpt_i] & 0x000FFFFFFFFFF000ULL);
                    for (int pd_i = 0; pd_i < 512; pd_i++) {
                        if (pd[pd_i] & PAGE_PRESENT) {
                            UINT64 *pt = (UINT64*)(UINTN)(pd[pd_i] & 0x000FFFFFFFFFF000ULL);
                            for (int pt_i = 0; pt_i < 512; pt_i++) {
                                if (pt[pt_i] & PAGE_PRESENT) {
                                    void *frame = (void*)(UINTN)(pt[pt_i] & 0x000FFFFFFFFFF000ULL);
                                    pmm_free_frame(frame);
                                }
                            }
                            pmm_free_frame(pt);
                        }
                    }
                    pmm_free_frame(pd);
                }
            }
            pmm_free_frame(pdpt);
        }
    }
    pmm_free_frame(pml4);
}

// Demand Paging Handler bei Page Fault (Exception 14)
int vmm_handle_page_fault(UINT64 *pml4, UINT64 fault_addr, UINT64 heap_start, UINT64 heap_end) {
    if (fault_addr >= heap_start && fault_addr < heap_end) {
        void *frame = pmm_alloc_frame();
        if (!frame) return 0; // OOM!
        
        UINT64 page_aligned = fault_addr & ~0xFFFULL;
        return vmm_map_page(pml4, page_aligned, (UINT64)(UINTN)frame, PAGE_WRITABLE | PAGE_USER);
    }
    return 0; // Ungültiger Speicherzugriff (Segfault)
}