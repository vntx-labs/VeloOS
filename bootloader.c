#include <efi.h>
#include <efilib.h>
#include <velo/bootinfo.h>

typedef struct {
    UINT8  e_ident[16];
    UINT16 e_type;
    UINT16 e_machine;
    UINT32 e_version;
    UINT64 e_entry;
    UINT64 e_phoff;
    UINT64 e_shoff;
    UINT32 e_flags;
    UINT16 e_ehsize;
    UINT16 e_phentsize;
    UINT16 e_phnum;
    UINT16 e_shentsize;
    UINT16 e_shnum;
    UINT16 e_shstrndx;
} __attribute__((packed)) Elf64_Ehdr;

typedef struct {
    UINT32 p_type;
    UINT32 p_flags;
    UINT64 p_offset;
    UINT64 p_vaddr;
    UINT64 p_paddr;
    UINT64 p_filesz;
    UINT64 p_memsz;
    UINT64 p_align;
} __attribute__((packed)) Elf64_Phdr;

typedef void (*KernelEntry)(BootInfo *info);

static BootInfo g_boot_info;
static UINT8 g_boot_stack[65536] __attribute__((aligned(16)));

static EFI_STATUS load_file(EFI_HANDLE image_handle, const CHAR16 *filename, VOID **buffer, UINTN *size) {
    EFI_STATUS status;
    EFI_LOADED_IMAGE *loaded_image = NULL;
    EFI_GUID loaded_image_guid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
    EFI_GUID sfsp_guid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *sfsp = NULL;
    EFI_FILE_PROTOCOL *root = NULL;
    EFI_FILE_PROTOCOL *file = NULL;

    status = uefi_call_wrapper(BS->HandleProtocol, 3, image_handle, &loaded_image_guid, (VOID**)&loaded_image);
    if (EFI_ERROR(status)) return status;

    status = uefi_call_wrapper(BS->HandleProtocol, 3, loaded_image->DeviceHandle, &sfsp_guid, (VOID**)&sfsp);
    if (EFI_ERROR(status)) return status;

    status = uefi_call_wrapper(sfsp->OpenVolume, 2, sfsp, &root);
    if (EFI_ERROR(status)) return status;

    status = uefi_call_wrapper(root->Open, 5, root, &file, (CHAR16*)filename, EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(status)) return status;

    EFI_FILE_INFO *info = NULL;
    UINTN info_size = sizeof(EFI_FILE_INFO) + 1024;
    info = (EFI_FILE_INFO*)AllocatePool(info_size);
    EFI_GUID info_guid = EFI_FILE_INFO_ID;
    status = uefi_call_wrapper(file->GetInfo, 4, file, &info_guid, &info_size, info);
    if (EFI_ERROR(status)) {
        FreePool(info);
        return status;
    }

    *size = info->FileSize;
    FreePool(info);

    *buffer = AllocatePool(*size);
    status = uefi_call_wrapper(file->Read, 3, file, size, *buffer);
    uefi_call_wrapper(file->Close, 1, file);
    uefi_call_wrapper(root->Close, 1, root);

    return status;
}

EFI_STATUS efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
    InitializeLib(ImageHandle, SystemTable);

    if (BS && BS->SetWatchdogTimer) {
        uefi_call_wrapper(BS->SetWatchdogTimer, 4, 0, 0, 0, NULL);
    }

    // 1. GOP initialisieren (1024x768)
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = NULL;
    EFI_GUID gop_guid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
    uefi_call_wrapper(BS->LocateProtocol, 3, &gop_guid, NULL, (VOID**)&gop);

    UINT32 best_mode = gop->Mode->Mode;
    for (UINT32 m = 0; m < gop->Mode->MaxMode; m++) {
        UINTN size_info = 0;
        EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info = NULL;
        if (uefi_call_wrapper(gop->QueryMode, 4, gop, m, &size_info, &info) == EFI_SUCCESS && info) {
            if (info->HorizontalResolution == 1024 && info->VerticalResolution == 768) {
                best_mode = m;
                break;
            }
        }
    }
    uefi_call_wrapper(gop->SetMode, 2, gop, best_mode);

    g_boot_info.magic = BOOT_MAGIC;
    g_boot_info.framebuffer_base = (uint64_t)gop->Mode->FrameBufferBase;
    g_boot_info.framebuffer_size = (uint64_t)gop->Mode->FrameBufferSize;
    g_boot_info.width = gop->Mode->Info->HorizontalResolution;
    g_boot_info.height = gop->Mode->Info->VerticalResolution;
    g_boot_info.pixels_per_scanline = gop->Mode->Info->PixelsPerScanLine ? gop->Mode->Info->PixelsPerScanLine : gop->Mode->Info->HorizontalResolution;

    // 2. KERNEL.ELF von der EFI-Partition laden
    VOID *elf_file = NULL;
    UINTN elf_size = 0;
    EFI_STATUS st = load_file(ImageHandle, L"\\KERNEL.ELF", &elf_file, &elf_size);
    if (EFI_ERROR(st)) {
        st = load_file(ImageHandle, L"\\EFI\\BOOT\\KERNEL.ELF", &elf_file, &elf_size);
    }
    if (EFI_ERROR(st) || !elf_file) {
        Print(L"[!] FEHLER: KERNEL.ELF konnte nicht geladen werden!\n");
        while(1);
    }

    Elf64_Ehdr *ehdr = (Elf64_Ehdr*)elf_file;
    if (ehdr->e_ident[0] != 0x7F || ehdr->e_ident[1] != 'E' || ehdr->e_ident[2] != 'L' || ehdr->e_ident[3] != 'F') {
        Print(L"[!] FEHLER: KERNEL.ELF hat keinen gueltigen ELF64-Header!\n");
        while(1);
    }

    // 3. ELF Segmente an Zieladressen laden
    Elf64_Phdr *phdrs = (Elf64_Phdr*)((UINT8*)elf_file + ehdr->e_phoff);
    for (UINT16 i = 0; i < ehdr->e_phnum; i++) {
        if (phdrs[i].p_type == 1) { // PT_LOAD
            UINTN pages = EFI_SIZE_TO_PAGES(phdrs[i].p_memsz);
            EFI_PHYSICAL_ADDRESS paddr = phdrs[i].p_paddr;

            st = uefi_call_wrapper(BS->AllocatePages, 4, AllocateAddress, EfiLoaderData, pages, &paddr);
            if (EFI_ERROR(st)) {
                st = uefi_call_wrapper(BS->AllocatePages, 4, AllocateAnyPages, EfiLoaderData, pages, &paddr);
            }

            VOID *dest = (VOID*)(UINTN)phdrs[i].p_vaddr;
            VOID *src = (VOID*)((UINT8*)elf_file + phdrs[i].p_offset);
            __builtin_memcpy(dest, src, phdrs[i].p_filesz);

            if (phdrs[i].p_memsz > phdrs[i].p_filesz) {
                __builtin_memset((UINT8*)dest + phdrs[i].p_filesz, 0, phdrs[i].p_memsz - phdrs[i].p_filesz);
            }
        }
    }

    UINT64 entry_point = ehdr->e_entry;

    // 4. Memory Map holen
    UINTN mmap_size = 0, map_key = 0, desc_size = 0;
    UINT32 desc_ver = 0;
    uefi_call_wrapper(BS->GetMemoryMap, 5, &mmap_size, NULL, &map_key, &desc_size, &desc_ver);
    mmap_size += 8192;

    UINTN pages = EFI_SIZE_TO_PAGES(mmap_size);
    EFI_PHYSICAL_ADDRESS mmap_phys = 0;
    uefi_call_wrapper(BS->AllocatePages, 4, AllocateAnyPages, EfiLoaderData, pages, &mmap_phys);
    EFI_MEMORY_DESCRIPTOR *mmap = (EFI_MEMORY_DESCRIPTOR*)(UINTN)mmap_phys;

    uefi_call_wrapper(BS->GetMemoryMap, 5, &mmap_size, mmap, &map_key, &desc_size, &desc_ver);

    UINT64 total_bytes = 0;
    UINTN num_entries = mmap_size / desc_size;
    for (UINTN i = 0; i < num_entries; i++) {
        EFI_MEMORY_DESCRIPTOR *desc = (EFI_MEMORY_DESCRIPTOR*)((UINT8*)mmap + i * desc_size);
        if (desc->Type == EfiConventionalMemory || desc->Type == EfiLoaderCode || desc->Type == EfiLoaderData ||
            desc->Type == EfiBootServicesCode || desc->Type == EfiBootServicesData) {
            total_bytes += desc->NumberOfPages * 4096ULL;
        }
    }

    g_boot_info.mmap_addr = (uint64_t)(UINTN)mmap;
    g_boot_info.mmap_size = (uint64_t)mmap_size;
    g_boot_info.mmap_desc_size = (uint64_t)desc_size;
    g_boot_info.mmap_desc_version = desc_ver;
    g_boot_info.total_ram_mb = total_bytes / (1024 * 1024);

    // 5. ExitBootServices
    while (1) {
        st = uefi_call_wrapper(BS->GetMemoryMap, 5, &mmap_size, mmap, &map_key, &desc_size, &desc_ver);
        if (EFI_ERROR(st)) continue;

        st = uefi_call_wrapper(BS->ExitBootServices, 2, ImageHandle, map_key);
        if (!EFI_ERROR(st)) break;
    }

    // 6. Stack setzen & in den Kernel springen
    UINT64 stack_top = (UINT64)(UINTN)(g_boot_stack + sizeof(g_boot_stack));
    stack_top = (stack_top & ~0xFULL) - 8;

    __asm__ volatile(
        "movq %0, %%rsp\n\t"
        "movq %1, %%rdi\n\t"
        "jmp *%2\n\t"
        :
        : "r"(stack_top), "r"(&g_boot_info), "r"(entry_point)
        : "memory"
    );

    while (1) {
        __asm__ volatile("hlt");
    }
    return EFI_SUCCESS;
}