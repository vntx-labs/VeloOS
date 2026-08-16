[org 0x7c00]
[bits 16]

start:
    cli                     ; Interrupts absolut ausschalten
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7c00          ; Stack unterhalb des Bootloaders (wächst nach unten)

    mov [boot_drive], dl

    ; A20-Gate aktivieren
    in al, 0x92
    or al, 2
    out 0x92, al

    ; --- ROBUSTER SEKTOR-READER (LDS / CHS Safe) ---
    mov bx, 0x7e00          ; Zieladresse im RAM (direkt hinter Stage 1)
    mov cl, 2               ; Starte bei Sektor 2 (Sektor 1 ist Stage 1)

.read_loop:
    mov ah, 0x02            ; BIOS Read Sector Funktion
    mov al, 1               ; 1 Sektor auf einmal lesen (sicherer für BIOS-Kompatibilität)
    mov ch, 0               ; Cylinder 0
    mov dh, 0               ; Head 0
    mov dl, [boot_drive]    

    push cx                 ; CX für Schleife sichern
    int 0x13
    jc disk_error           ; Bei Fehler abbrechen
    pop cx                  ; CX wiederherstellen

    add bx, 512             ; Zieladresse um 512 Bytes erhöhen
    inc cl                  ; Nächster Sektor
    
    ; Hinweis: Standard CHS unterstützt nur 18 Sektoren pro Spur (Disketten) oder mehr bei Festplatten.
    ; Für große Stage-2-Dateien empfiehlt sich langfristig LBA (AH=0x42).
    cmp cl, 18              ; Beispiel-Limit für eine Spur (oder entsprechend anpassen)
    jle .read_loop

    ; Wenn alles geladen ist, Sprung in Stage 2
    cli 
    jmp 0x0000:0x7e00

disk_error:
    pop cx                  ; Stack korrigieren, falls der Fehler nach dem push auftrat
    mov ax, 0xb800
    mov es, ax
    mov byte [es:0], 'E'
    mov byte [es:1], 0x4F   
.hang:
    cli
    hlt
    jmp .hang

boot_drive: db 0

times 510-($-$$) db 0
dw 0xaa55
