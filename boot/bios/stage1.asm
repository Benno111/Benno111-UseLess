
org 0x7C00
bits 16

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00

    sti

    ; Save boot drive
    mov [boot_drive], dl

    ; -------------------------
    ; Check INT13 extensions
    ; -------------------------
    mov ah, 0x41
    mov bx, 0x55AA
    mov dl, [boot_drive]
    int 0x13
    jc no_lba
    cmp bx, 0xAA55
    jne no_lba

    mov byte [use_lba], 1
    jmp load_stage2

no_lba:
    mov byte [use_lba], 0

; -------------------------
; Load stage2
; -------------------------
load_stage2:

    cmp byte [use_lba], 1
    je load_lba

    jmp load_fail   ; CHS fallback not implemented yet

; -------------------------
; LBA read with retry
; -------------------------
load_lba:

    mov si, 3              ; retry count

.read_retry:
    mov ah, 0x42           ; extended read
    mov dl, [boot_drive]
    mov si, dap
    int 0x13
    jnc .read_ok

    ; failure → retry
    dec si
    jz load_fail

    ; reset disk
    mov ah, 0x00
    mov dl, [boot_drive]
    int 0x13

    ; small delay (helps on some BIOS)
    mov cx, 0xFFFF
.delay:
    loop .delay

    jmp .read_retry

.read_ok:
    jmp 0x0000:0x7E00      ; jump to stage2

; -------------------------
; Failure handler
; -------------------------
load_fail:
    mov si, disk_error_msg
    call print_string

.hang:
    cli
    hlt
    jmp .hang

; -------------------------
; Print string (BIOS)
; -------------------------
print_string:
    mov ah, 0x0E
.next:
    lodsb
    cmp al, 0
    je .done
    int 0x10
    jmp .next
.done:
    ret

; -------------------------
; Data
; -------------------------
boot_drive db 0
use_lba db 0

disk_error_msg db "Disk read error", 0

; -------------------------
; Disk Address Packet (DAP)
; -------------------------
dap:
    db 0x10            ; size
    db 0
stage2_sector_count:
    dw 16              ; will be patched by build script
    dw 0x7E00          ; offset
    dw 0x0000          ; segment
    dq 1               ; LBA start (sector 1)

; -------------------------
; Boot signature
; -------------------------
times 510-($-$$) db 0
dw 0xAA55
