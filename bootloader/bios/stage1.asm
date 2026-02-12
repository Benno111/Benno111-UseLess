BITS 16
ORG 0x7C00

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti

    mov si, s1_msg
    call print

    mov [boot_drive], dl

    mov bx, 0x8000
    mov si, 1
    call disk_read_lba
    jc disk_error

    jmp 0x0000:0x8000

disk_error:
    mov si, err_msg
    call print
    hlt
    jmp $

print:
    lodsb
    test al, al
    jz .done
    mov ah, 0x0E
    int 0x10
    jmp print
.done:
    ret

disk_read_lba:
    pusha
    mov ah, 0x42
    mov dl, [boot_drive]
    mov word [dap+2], STAGE2_SECTORS
    mov word [dap+4], bx
    mov word [dap+6], 0x0000
    mov word [dap+8], si
    mov word [dap+10], 0
    mov dword [dap+12], 0x00000000
    mov si, dap
    int 0x13
    popa
    ret

boot_drive: db 0
err_msg: db "Boot error", 0
s1_msg: db "S1", 0

dap:
    db 0x10
    db 0
    dw 0
    dw 0
    dw 0
    dq 0

%ifndef STAGE2_SECTORS
%define STAGE2_SECTORS 64
%endif

times 510-($-$$) db 0
dw 0xAA55
