#ifndef _VELO_BOOTINFO_H
#define _VELO_BOOTINFO_H

#include <stdint.h>

#define BOOT_MAGIC 0x56454C4F5F4F5321ULL // "VELO_OS!"

typedef struct __attribute__((packed)) {
    uint64_t magic;
    
    // Framebuffer-Informationen (GOP)
    uint64_t framebuffer_base;
    uint64_t framebuffer_size;
    uint32_t width;
    uint32_t height;
    uint32_t pixels_per_scanline;

    // Speicher-Informationen (UEFI Memory Map)
    uint64_t mmap_addr;
    uint64_t mmap_size;
    uint64_t mmap_desc_size;
    uint32_t mmap_desc_version;
    uint64_t total_ram_mb;

    // ACPI / System
    uint64_t rsdp_addr;
} BootInfo;

#endif