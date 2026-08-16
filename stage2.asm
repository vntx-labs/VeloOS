[bits 16]

global stage2_start         ; Macht das Einstiegslabel für dein Linker-Skript sichtbar

section .text

stage2_start:
    cli
    
    ; --- Visuelles Feedback: Stage 2 erreicht (Real Mode) ---
    mov ax, 0xb800
    mov es, ax
    mov byte [es:0], '2'    
    mov byte [es:1], 0x4F   

    ; LÖSUNG 1: dword zwingt NASM, eine 32-Bit-Adresse im 16-Bit-Code zu generieren
    mov eax, dword gdt_start
    mov dword [gdt_descriptor + 2], eax
    lgdt [gdt_descriptor]

    ; Protected Mode aktivieren (Bit 0 in CR0)
    mov eax, cr0
    or eax, 1
    mov cr0, eax

    ; LÖSUNG 2: dword zwingt NASM zu einem 32-Bit-Far-Jump (verhindert 16-Bit-Abschneidung)
    jmp dword 0x08:protected_mode_start

[bits 32]
protected_mode_start:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    
    ; Veffizienter Stack-Schutz (Über 512 KB verankert)
    mov esp, 0x90000             

    ; Paging-Puffer bei 0x20000 (128 KB) vorbereiten (4 KB nullen reicht)
    mov edi, 0x20000
    mov ecx, 1024                
    xor eax, eax
    cld
    rep stosd

    ; 1-GB-Identity-Mapping bei 0x20000 aufbauen
    ; PML4 (bei 0x20000) -> zeigt auf PDPT (bei 0x21000)
    mov dword [0x20000], 0x21003  
    
    ; PDPT (bei 0x21000) -> Direktes Mapping einer 1-GB Huge Page (Bit 7 = 0x80)
    mov dword [0x21000], 0x00000083 
    mov dword [0x21004], 0

    ; CR3 on PML4 zeigen lassen
    mov eax, 0x20000
    mov cr3, eax

    ; PAE aktivieren (CR4 Bit 5)
    mov eax, cr4
    or eax, 1 << 5              
    mov cr4, eax

    ; Long Mode Bit im EFER MSR setzen (0xC0000080, Bit 8)
    mov ecx, 0xC0000080         
    rdmsr
    or eax, 1 << 8              
    wrmsr

    ; Paging einschalten (CR0 Bit 31) -> Wechsel in den Long Mode
    mov eax, cr0
    or eax, 1 << 31
    mov cr0, eax

    ; Finaler Sprung in das 64-Bit Code-Segment (Index 0x18 in der GDT)
    jmp 0x18:long_mode_start

[bits 64]
long_mode_start:
    ; 64-Bit Datensegmente laden (0x20 ist der Datendeskriptor)
    mov ax, 0x20
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; 64-Bit Stack flugs auf 0x90000 ziehen. 
    mov rax, 0x90000             
    mov rsp, rax                
    mov rbp, rax                

    ; --- IDT ARBEITSSPEICHER EINRICHTEN ---
    mov edi, 0x1000
    mov ecx, 192                    
    xor eax, eax
    cld
    rep stosd

    ; IDT über deinen statischen Descriptor am Dateiende laden
    lidt [idt64_descriptor]

    ; --- IN DEN C-KERNEL SPRINGEN ---
    extern kernel_main          
    call kernel_main            

.kernel_gate:
    cli
    hlt
    jmp .kernel_gate

; =====================================================================
; GLOBALE TASTATUR INTERRUPT SERVICEROUTINE (ISR) WRAPPER
; =====================================================================
global keyboard_isr_wrapper
extern keyboard_interrupt_handler

keyboard_isr_wrapper:
    ; Alle flüchtigen Scratch-Register sichern (64-Bit System V ABI)
    push rax
    push rcx
    push rdx
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11

    cld                                 ; C-Compiler erwartet gelöschtes Direction-Flag
    call keyboard_interrupt_handler     ; Ruft die C-Handlerfunktion auf

    ; Register im umgekehrten Zustand wiederherstellen
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rax
    iretq                               ; 64-Bit Interrupt Return (wichtig!)


section .data

; =====================================================================
; COMBINED GLOBAL DESCRIPTOR TABLE
; =====================================================================
align 8
gdt_start:
    dq 0x0000000000000000       ; Null-Deskriptor

    ; 32-Bit Deskriptoren (für den Umschaltprozess)
    dq 0x00cf9a000000ffff       ; 0x08: 32-Bit Code
    dq 0x00cf92000000ffff       ; 0x10: 32-Bit Daten

    ; 64-Bit Deskriptoren (für das eigentliche System)
    dq 0x00209a0000000000       ; 0x18: 64-Bit Code
    dq 0x0000920000000000       ; 0x20: 64-Bit Daten
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd 0                        
; =====================================================================

align 8
idt64_descriptor:
    dw (48 * 16) - 1            
    dq 0x1000
