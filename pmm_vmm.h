#ifndef PMM_VMM_H
#define PMM_VMM_H

#include <efi.h>
#include <efilib.h>

#define PAGE_SIZE 4096ULL

// Bit-Flags für x86_64 Paging
#define PAGE_PRESENT  (1ULL << 0)
#define PAGE_WRITABLE (1ULL << 1)
#define PAGE_USER     (1ULL << 2)

// Initialisierung des PMM aus der UEFI Memory Map
void pmm_init(EFI_MEMORY_DESCRIPTOR *mmap, UINTN map_size, UINTN desc_size);
void* pmm_alloc_frame(void);
void pmm_free_frame(void *phys_addr);
UINT64 pmm_get_free_memory_bytes(void);

// Virtueller Speicher & Adressraum-Management
UINT64* vmm_create_address_space(void);
void vmm_destroy_address_space(UINT64 *pml4);
int vmm_map_page(UINT64 *pml4, UINT64 virt_addr, UINT64 phys_addr, UINT64 flags);
void vmm_unmap_page(UINT64 *pml4, UINT64 virt_addr);
UINT64 vmm_get_phys_addr(UINT64 *pml4, UINT64 virt_addr);

// On-Demand Page Fault Handler für Heap & Stack
int vmm_handle_page_fault(UINT64 *pml4, UINT64 fault_addr, UINT64 heap_start, UINT64 heap_end);

#endif